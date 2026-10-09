"""Independent peer-audit regressions for evidence and controller contracts.

These fixtures do not invoke compilers, provider SDKs, authentication or remote jobs.
"""
import copy
import hashlib
import json

import pytest

from qstack.artifacts import artifact_hash, load_artifact, numerical_evidence, save_artifact
from qstack.classical import extract_requirements, validate_requirements
from qstack.jobs import annotate_application_result
from qstack.models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError, SubmissionOptions
from qstack.registry import canonical_json, digest, validate_physical


def compiled(version=2):
    ir = {"schema_version": version, "num_qubits": 1, "num_clbits": 0,
          "instructions": [], "global_phase": 0}
    artifact = CompiledArtifact("ibm", "audit", "qasm3", "OPENQASM 3.0; qubit[1] q;", ir,
        schema_version=version, logical_ir=copy.deepcopy(ir) if version == 2 else None)
    artifact.manifest = {
        "native_spinor": "target audit\nqubit q[1]\n", "compiler_version": "audit-version",
        "source_hash": "source-hash", "optimization_level": 2, "seed": 42,
        "statistics": {"gate_count": 0}, "readout_order": "c[n-1]...c[0]",
        "qir_entry_point": None, "approximation_error_budget": 0,
    }
    if version == 2:
        artifact.numerical_report = {"schema_version": 1, "certified": False, "whole_program_error": None,
            "coverage": {"complete": False, "gaps": ["No end-to-end certificate"]},
            "logical_ir_hash": digest(artifact.logical_ir), "physical_ir_hash": digest(ir)}
    return artifact


def test_v1_complete_historical_preimage_stays_frozen(tmp_path):
    old = compiled(1)
    # Independent literal preimage, including every historical compilation key.
    preimage = {"schema_version": 1, "route": "ibm", "target": "audit", "format": "qasm3",
        "payload_hash": hashlib.sha256(old.payload.encode()).hexdigest(),
        "physical_ir": copy.deepcopy(old.physical_ir), "target_snapshot": {},
        "compilation": copy.deepcopy(old.manifest)}
    expected = hashlib.sha256(canonical_json(preimage)).hexdigest()
    old.manifest.update(limits={"new": 1}, comparison={"new": 2},
                        compiler_tools={"new": "ignored-by-v1"}, source_bytes_sha256="ignored-by-v1")
    assert artifact_hash(old) == expected
    restored = load_artifact(save_artifact(old, tmp_path / "v1"))
    assert artifact_hash(restored) == expected
    assert numerical_evidence(restored)["whole_program_error"] is None
    assert numerical_evidence(restored)["coverage"]["complete"] is False


@pytest.mark.parametrize("field", ["source_hash", "source_bytes_sha256", "compiler_tools", "logical", "physical", "numerical", "requirements"])
def test_v2_hash_binds_source_and_each_evidence_component(field):
    artifact = compiled()
    original = artifact_hash(artifact)
    if field in {"source_hash", "source_bytes_sha256"}: artifact.manifest[field] = "different-source"
    elif field == "compiler_tools": artifact.manifest[field] = {"spinorc": {"sha256": "different-binary", "size": 123}}
    elif field == "logical": artifact.logical_ir["instructions"] = [{"op": "x", "qubits": [0]}]
    elif field == "physical": artifact.physical_ir["instructions"] = [{"op": "x", "qubits": [0]}]
    elif field == "numerical": artifact.numerical_report["whole_program_error"] = 0
    else: artifact.feature_requirements = {"features": ["classical.add"], "integer_widths": [8]}
    assert artifact_hash(artifact) != original


def test_rehashing_numerical_file_does_not_bypass_artifact_binding(tmp_path):
    path = save_artifact(compiled(), tmp_path / "bundle")
    numerical = json.loads((path / "numerical.json").read_bytes())
    numerical["whole_program_error"] = 0
    numerical["logical_ir_hash"] = "unrelated-logical-source"
    (path / "numerical.json").write_bytes(canonical_json(numerical))
    manifest = json.loads((path / "manifest.json").read_bytes())
    manifest["files"]["numerical.json"] = hashlib.sha256((path / "numerical.json").read_bytes()).hexdigest()
    (path / "manifest.json").write_bytes(canonical_json(manifest))
    with pytest.raises(QStackError, match="manifest hash mismatch"):
        load_artifact(path)


@pytest.mark.parametrize("counts", [{}, {"0": 0}])
def test_empty_shot_evidence_never_reports_application_completed(counts):
    receipt = JobReceipt("ibm", "audit", "job", metadata={"application_outputs": [
        {"name": "loop_exhausted", "role": "loop_exhausted", "bits": [0]}]})
    result = ExecutionResult("ibm", "audit", "job", counts, raw={"unchanged": True})
    annotate_application_result(receipt, result)
    assert result.metadata["application_status"] == "not_checked"
    assert result.counts == counts and result.raw == {"unchanged": True}


def test_initialized_integer_inputs_still_require_width_support():
    ir = {"schema_version": 2, "num_qubits": 0, "num_clbits": 129,
          "classical_values": [
              {"id": "a", "type": "uint", "width": 64, "storage": list(range(64)), "initialized": True, "initial_value": "1"},
              {"id": "b", "type": "uint", "width": 64, "storage": list(range(64, 128)), "initialized": True, "initial_value": "2"},
              {"id": "equal", "type": "bool", "width": 1, "storage": [128]}],
          "instructions": [{"op": "c_eq", "result": "equal", "inputs": ["a", "b"]}]}
    requirements = extract_requirements(ir)
    assert requirements["integer_widths"] == [64]
    unsupported = {"capabilities": {"features": {"classical.eq": "supported"}, "integer_widths": [8]}}
    supported = {"capabilities": {"features": {"classical.eq": "supported"}, "integer_widths": [64]}}
    with pytest.raises(QStackError, match="device.*integer widths"):
        validate_requirements(requirements, unsupported, supported)


def test_typed_output_requires_explicit_output_contract():
    ir = {"schema_version": 2, "num_qubits": 1, "num_clbits": 1,
          "classical_values": [{"id": "flag", "type": "bool", "width": 1, "storage": [0]}],
          "classical_outputs": [{"name": "flag", "value": "flag", "type": "bool", "width": 1}],
          "instructions": [{"op": "measure", "result": "flag", "qubits": [0], "clbits": [0]}]}
    requirements = extract_requirements(ir)
    assert "output.classical" in requirements["features"]
    legacy = {"supports": {"feedforward": "full", "mid_circuit_measure": True}}
    supported = {"capabilities": {"features": {"output.classical": "supported", "measure": "supported"}}}
    with pytest.raises(QStackError, match="device.*output.classical.*unknown"):
        validate_requirements(requirements, legacy, supported)


@pytest.mark.parametrize("version", [1, 2])
@pytest.mark.parametrize("name", ["optimization.json", "mappings.json", "native.spinor"])
def test_rehashing_derived_sidecar_cannot_change_core_evidence(tmp_path, version, name):
    path = save_artifact(compiled(version), tmp_path / "bundle")
    manifest = json.loads((path / "manifest.json").read_bytes())
    original_hash = manifest["artifact_hash"]
    replacement = b"unrelated native program\n" if name == "native.spinor" else canonical_json({"fabricated": True})
    (path / name).write_bytes(replacement)
    manifest["files"][name] = hashlib.sha256(replacement).hexdigest()
    (path / "manifest.json").write_bytes(canonical_json(manifest))
    assert manifest["artifact_hash"] == original_hash
    with pytest.raises(QStackError, match="disagrees") as error:
        load_artifact(path)
    assert error.value.code == "ARTIFACT_INVALID"


def test_v2_classical_sidecar_is_required_and_matches_sealed_ir(tmp_path):
    path = save_artifact(compiled(), tmp_path / "bundle")
    manifest = json.loads((path / "manifest.json").read_bytes())
    replacement = canonical_json({"classical_outputs": [{"name": "fabricated"}]})
    (path / "classical.json").write_bytes(replacement)
    manifest["files"]["classical.json"] = hashlib.sha256(replacement).hexdigest()
    (path / "manifest.json").write_bytes(canonical_json(manifest))
    with pytest.raises(QStackError, match="sidecar disagrees"):
        load_artifact(path)
    del manifest["files"]["classical.json"]
    (path / "classical.json").unlink()
    (path / "manifest.json").write_bytes(canonical_json(manifest))
    with pytest.raises(QStackError) as error:
        load_artifact(path)
    assert error.value.code == "ARTIFACT_INVALID"


def test_cassette_without_correlated_flags_does_not_claim_loop_completion(tmp_path, monkeypatch):
    from importlib.resources import files
    from qstack.jobs import load_job
    from qstack.service import submit_artifact
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    artifact = compiled()
    artifact.physical_ir.update(num_clbits=1, classical_values=[
        {"id": "exhausted", "type": "bool", "width": 1, "storage": [0], "visibility": "exported",
         "initialized": True, "initial_value": "0"}], classical_outputs=[
        {"name": "loop_exhausted", "value": "exhausted", "type": "bool", "width": 1, "role": "loop_exhausted"}],
        exported_clbits=[0])
    artifact.target_snapshot = {"qubits": 1, "all_to_all": True, "capabilities": {"features": {
        "output.classical": "supported", "output.loop_exhausted": "supported"}}}
    result = submit_artifact(artifact, SubmissionOptions(mode="cassette", name="bell"), wait=True)
    expected_raw = json.loads(files("spinor_submit").joinpath("cassettes", "ibm", "bell.json").read_text())
    assert result.counts is None and result.raw == expected_raw
    assert result.metadata["application_status"] == "not_checked"
    receipt, restored = load_job(result.job_id)
    assert receipt.metadata["application_outputs"][0]["bits"] == [0]
    assert restored.metadata["application_status"] == "not_checked"
    assert restored.raw == expected_raw and restored.counts is None


@pytest.mark.parametrize("explicit,legacy,accepted", [("supported", "none", True), ("unsupported", "full", False), ("unknown", "full", False)])
def test_explicit_branch_contract_overrides_legacy_feedforward(explicit, legacy, accepted):
    ir = {"schema_version": 1, "num_qubits": 1, "num_clbits": 1, "instructions": [
        {"op": "if", "clbits": [0]}, {"op": "x", "qubits": [0]}, {"op": "endif"}]}
    target = {"qubits": 1, "all_to_all": True, "native_gates": ["x"], "supports": {"feedforward": legacy},
              "capabilities": {"features": {"branching.bit": explicit}}}
    if accepted:
        validate_physical(ir, target)
    else:
        with pytest.raises(QStackError):
            validate_physical(ir, target)


@pytest.mark.parametrize("feature", ["measure", "reset", "branching.bit"])
def test_explicit_unknown_capabilities_are_not_inferred_from_legacy(feature):
    from qstack.classical import feature_support
    target = {"supports": {"reset": True, "feedforward": "full"},
              "capabilities": {"features": {feature: "unknown"}}}
    assert feature_support(target, feature) == "unknown"
    with pytest.raises(QStackError, match="device.*unknown"):
        validate_requirements({"features": [feature]}, target,
                              {"capabilities": {"features": {feature: "supported"}}})


@pytest.mark.parametrize("initializer_present", [True, False])
def test_emitted_program_must_supply_its_own_classical_initialization(initializer_present):
    from qstack.verification import verify_artifact
    artifact = compiled()
    ir = artifact.physical_ir
    ir.update(num_clbits=1, classical_storage=[
        {"id": "s0", "width": 1, "bits": [0], "visibility": "exported", "initialized": True, "initial_value": "1"}],
        classical_values=[{"id": "flag", "type": "bool", "width": 1, "storage": [0],
                           "visibility": "exported", "initialized": True, "initial_value": "1"}],
        classical_outputs=[{"name": "flag", "value": "flag", "type": "bool", "width": 1}], exported_clbits=[0])
    artifact.logical_ir = copy.deepcopy(ir)
    artifact.payload = "OPENQASM 3.0; qubit[1] q; bit[1] c;" + (" c[0] = 1;" if initializer_present else "")
    evidence = verify_artifact(artifact, store=False)
    emitted = next(check for check in evidence["checks"] if check["name"] == "physical_to_program")
    assert emitted["status"] == ("passed" if initializer_present else "failed")
    if not initializer_present:
        assert evidence["coverage_complete"] is False
