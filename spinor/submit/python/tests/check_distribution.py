"""Verify a built provider wheel from an isolated install outside the checkout.

Usage: python tests/check_distribution.py package.whl [--output evidence.json]
Only pip installation uses the network. No compiler, authentication, discovery,
provider validation job or hardware submission is run by these checks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import venv
import zipfile


CORE_MODULES = (
    "artifacts", "classical", "cli", "jobs", "models", "registry", "scheduling",
    "target_models", "validation_harness", "verification.__init__",
    "verification.engine", "verification.matrices", "verification.parsers",
    "verification.qir", "verification.sdk",
)

REGISTRY_CHECK = r'''
import importlib, importlib.metadata, json, sys
from pathlib import Path
from qstack.registry import registry_root, profiles
from qstack.providers import ADAPTERS
import qstack
root = registry_root()
assert root == Path(qstack.__file__).parent / 'data' / 'registry', root
assert len(profiles()) == 31
assert {'ibm', 'google', 'ionq', 'aws', 'azure'} <= set(ADAPTERS)
for name in ('artifacts', 'classical', 'models', 'target_models', 'scheduling', 'validation_harness', 'verification'):
    module = importlib.import_module('qstack.' + name)
    assert Path(module.__file__).is_relative_to(Path(sys.prefix)), module.__file__
assert Path(qstack.__file__).is_relative_to(Path(sys.prefix)), qstack.__file__
print(json.dumps({'profiles': len(profiles()), 'routes': sorted(ADAPTERS),
                  'version': importlib.metadata.version('heisenberg-spinor-submit'),
                  'installed_outside_checkout': True}))
'''

ARTIFACT_CHECK = r'''
import copy, hashlib, importlib, importlib.metadata, json, socket, sys
from pathlib import Path
from qstack.artifacts import artifact_hash, load_artifact, numerical_evidence, save_artifact
from qstack.classical import extract_requirements, validate_classical
from qstack.models import CompiledArtifact
from qstack.registry import canonical_json, digest
from qstack.verification import verify_artifact

def forbidden(*args, **kwargs):
    raise AssertionError('Offline package verification attempted external execution')
socket.create_connection = forbidden
socket.socket.connect = forbidden
import qstack.service
qstack.service._process = forbidden
for name in ('engine', 'matrices', 'parsers', 'qir', 'sdk'):
    module = importlib.import_module('qstack.verification.' + name)
    assert Path(module.__file__).is_relative_to(Path(sys.prefix)), module.__file__

legacy_ir = {'schema_version': 1, 'num_qubits': 1, 'num_clbits': 0, 'instructions': [], 'global_phase': 0}
legacy = CompiledArtifact('ibm', 'offline', 'qasm3', 'OPENQASM 3.0; qubit[1] q;', legacy_ir)
preimage = {'schema_version': 1, 'route': 'ibm', 'target': 'offline', 'format': 'qasm3',
            'payload_hash': hashlib.sha256(legacy.program_bytes()).hexdigest(),
            'physical_ir': legacy_ir, 'target_snapshot': {}, 'compilation': {}}
expected = hashlib.sha256(canonical_json(preimage)).hexdigest()
legacy.manifest['compiler_tools'] = {'new_v2_field': 'must_not_change_v1_hash'}
assert artifact_hash(legacy) == expected
restored = load_artifact(save_artifact(legacy, 'legacy artifact'))
assert artifact_hash(restored) == expected
assert numerical_evidence(restored)['whole_program_error'] is None
assert numerical_evidence(restored)['certified'] is False

ir = {'schema_version': 2, 'num_qubits': 1, 'num_clbits': 0, 'global_phase': .3,
      'instructions': [{'op': 'rx', 'qubits': [0], 'params': [.2]}]}
artifact = CompiledArtifact('ibm', 'offline', 'qasm3',
    'OPENQASM 3.0; qubit[1] q; gphase(0.3); rx(0.2) q[0];', ir,
    schema_version=2, logical_ir=copy.deepcopy(ir))
artifact.numerical_report = {'schema_version': 1, 'certified': False, 'whole_program_error': None,
    'approximation_error_budget': 0, 'coverage': {'complete': False, 'gaps': ['Package fixture; no compiler observations']},
    'logical_ir_hash': digest(ir), 'physical_ir_hash': digest(ir)}
save_artifact(artifact, 'valid artifact v2')
restored = load_artifact('valid artifact v2')
assert artifact_hash(restored) == artifact_hash(artifact)
assert restored.logical_ir == ir and restored.numerical_report == artifact.numerical_report
result = verify_artifact(restored)
assert result['status'] == 'passed' and result['coverage_complete'], result
assert not result['certified'] and result['whole_program_error'] is None
assert not result['network_used'] and Path(result['evidence_path']).is_file()

# Importing the interpreter alone would not check its native PyQIR dependency.
import pyqir
qir_text = """%Qubit = type opaque
define void @main() {
entry:
  call void @__quantum__qis__rx__body(double 2.000000e-01, %Qubit* null)
  ret void
}
declare void @__quantum__qis__rx__body(double, %Qubit*)
"""
module = pyqir.Module.from_ir(pyqir.Context(), qir_text)
assert module.verify() is None
for format, payload in (('qir-text', qir_text), ('qir-bitcode', module.bitcode)):
    qir_artifact = copy.deepcopy(artifact)
    qir_artifact.format, qir_artifact.payload = format, payload
    qir_artifact.manifest['qir_entry_point'] = 'main'
    save_artifact(qir_artifact, format + ' artifact v2')
    check = verify_artifact(qir_artifact, store=False)
    assert check['status'] == 'passed' and not check['coverage_complete'], check
    assert check['checks'][1]['coverage_limits'], 'Omitted scalar phase must remain explicit'

mutation = copy.deepcopy(artifact)
mutation.payload = mutation.payload.replace('rx(0.2)', 'rx(0.21)')
save_artifact(mutation, 'wrong angle v2')
assert verify_artifact(mutation, store=False)['status'] == 'failed'

wide = {'schema_version': 2, 'num_qubits': 0, 'num_clbits': 64, 'global_phase': 0,
    'classical_values': [{'id': 'maximum', 'type': 'uint', 'width': 64, 'storage': list(range(64)),
                          'visibility': 'exported'}],
    'classical_outputs': [{'name': 'maximum', 'value': 'maximum', 'type': 'uint', 'width': 64}],
    'exported_clbits': list(range(64)),
    'instructions': [{'op': 'c_const', 'result': 'maximum', 'inputs': [], 'value': '18446744073709551615'}]}
payload = 'OPENQASM 3.0; bit[64] c; uint[64] value = 18446744073709551615; ' + ' '.join(
    f'c[{bit}] = (value >> {bit}) & 1;' for bit in range(64))
typed = CompiledArtifact('ibm', 'offline', 'qasm3', payload, wide, schema_version=2, logical_ir=copy.deepcopy(wide))
validate_classical(wide)
typed.feature_requirements = extract_requirements(wide)
save_artifact(typed, 'wide classical v2')
typed = load_artifact('wide classical v2')
assert typed.physical_ir['instructions'][0]['value'] == '18446744073709551615'
assert typed.feature_requirements['integer_widths'] == [64]
checked = verify_artifact(typed)
assert checked['status'] == 'passed', checked

large = copy.deepcopy(artifact)
large.physical_ir['num_qubits'] = large.logical_ir['num_qubits'] = 5
large.payload = large.payload.replace('qubit[1]', 'qubit[5]')
large.numerical_report.update(logical_ir_hash=digest(large.logical_ir), physical_ir_hash=digest(large.physical_ir))
save_artifact(large, 'over budget v2')
assert verify_artifact(large, store=False)['status'] == 'not_checked'
print(json.dumps({'v1_hash_compatible': True, 'v2_sidecars_loaded': True, 'exact_uint64': True,
                  'complete_phase_verified': True, 'mutation_rejected': True, 'budget_not_checked': True,
                  'qir_text_and_bitcode': True,
                  'compiler_used': False, 'network_used': False,
                  'dependencies': {name: importlib.metadata.version(name) for name in ('numpy', 'pyqir')}}))
'''


def main(wheel: str, output: str | None = None) -> dict:
    wheel_path = Path(wheel).resolve()
    evidence = {"schema_version": 1, "wheel": wheel_path.name,
                "wheel_sha256": hashlib.sha256(wheel_path.read_bytes()).hexdigest(),
                "wheel_size_bytes": wheel_path.stat().st_size, "checks": []}
    with zipfile.ZipFile(wheel_path) as archive:
        names = set(archive.namelist())
        chips = [name for name in names if name.startswith("qstack/data/registry/chips/") and name.endswith(".yaml")]
        topologies = [name for name in names if name.startswith("qstack/data/registry/topologies/") and name.endswith(".yaml")]
        assert len(chips) == 31, f"Expected 31 registry profiles in wheel, got {len(chips)}"
        assert topologies, "Target topologies are absent from the wheel"
        assert "qstack/providers/CONTRACTS.md" in names
        assert "qstack/LICENSE" in names
        assert any("/cassettes/" in name and name.endswith(".json") for name in names)
        for module in CORE_MODULES:
            assert "qstack/" + module.replace(".", "/") + ".py" in names, f"Missing packaged module: {module}"
        entrypoints = [archive.read(n).decode() for n in names if n.endswith(".dist-info/entry_points.txt")]
        assert any("qstack = qstack.cli:main" in text for text in entrypoints)
        evidence["checks"].append({"name": "wheel_contents", "status": "passed", "profiles": len(chips),
                                   "required_modules": list(CORE_MODULES)})
    with tempfile.TemporaryDirectory(prefix="qstack-wheel-check-") as directory:
        temp = Path(directory)
        environment = temp / "environment"
        venv.EnvBuilder(with_pip=True).create(environment)
        python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        cli = environment / ("Scripts/qstack.exe" if os.name == "nt" else "bin/qstack")
        env = {k: v for k, v in os.environ.items() if not k.startswith("QSTACK_") and k not in
               {"PYTHONPATH", "PYTHONHOME", "SPINOR_REGISTRY_ROOT"}}
        env["QSTACK_CONFIG"] = str(temp / "isolated config.toml")
        Path(env["QSTACK_CONFIG"]).write_text("", encoding="utf-8")
        env["QSTACK_STATE_DIR"] = str(temp / "private state")
        for driver in ("SPINORC", "PHOTONC", "PHONONC"):
            env["QSTACK_" + driver] = str(temp / ("unavailable-" + driver.lower()))

        def run(name, args, expected=0, json_output=False):
            completed = subprocess.run([str(arg) for arg in args], cwd=temp, env=env,
                                       text=True, capture_output=True, timeout=600)
            if completed.returncode != expected:
                raise AssertionError(f"{name}: expected exit {expected}, got {completed.returncode}\n"
                                     f"{completed.stdout}\n{completed.stderr}")
            entry = {"name": name, "status": "passed", "exit_code": completed.returncode}
            if json_output:
                entry["result"] = json.loads(completed.stdout)
            evidence["checks"].append(entry)
            return completed.stdout

        run("base_install", [python, "-m", "pip", "install", "--disable-pip-version-check", wheel_path])
        run("installed_registry_and_modules", [python, "-I", "-c", REGISTRY_CHECK], json_output=True)
        assert "verify" in run("console_entrypoint", [cli, "--help"])
        assert "verify" in run("verify_help_without_optional_dependencies", [python, "-I", "-m", "qstack", "verify", "--help"])
        run("verification_extra_install", [python, "-m", "pip", "install", "--disable-pip-version-check", str(wheel_path) + "[verify]"])
        run("installed_artifact_and_verifier_contracts", [python, "-I", "-c", ARTIFACT_CHECK], json_output=True)
        for artifact, status, code in (("valid artifact v2", "passed", 0), ("wrong angle v2", "failed", 1),
                                       ("over budget v2", "not_checked", 3), ("wide classical v2", "passed", 0),
                                       ("qir-text artifact v2", "passed", 0), ("qir-bitcode artifact v2", "passed", 0)):
            result = json.loads(run("cli_" + artifact, [cli, "verify", temp / artifact, "--no-env-file", "--json"], code))
            assert result["status"] == status and result["network_used"] is False
            assert result["certified"] is False and result["whole_program_error"] is None
            evidence["checks"][-1]["verification_status"] = status
    evidence["status"] = "passed"
    if output:
        destination = Path(output).resolve()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))
    return evidence


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wheel")
    parser.add_argument("--output", help="Write portable wheel-hash and isolated-install evidence")
    arguments = parser.parse_args()
    main(arguments.wheel, arguments.output)
