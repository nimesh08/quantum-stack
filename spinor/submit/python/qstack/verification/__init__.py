"""Offline independent finite-program verification; no provider or compiler calls."""
from __future__ import annotations

import hashlib
import importlib.metadata
import platform
from pathlib import Path

from ..artifacts import artifact_hash
from ..models import QStackError, VERIFICATION_VERSION
from ..registry import canonical_json, digest


class NotChecked(Exception):
    """The requested program exceeds this independent oracle's declared domain."""


MAX_INTERFACE_QUBITS = 65536
MAX_CLASSICAL_BITS = 4096
MAX_INSTRUCTIONS = 100000


def _interface_limits(ir):
    """Check declared widths before any SDK register/list or oracle allocation."""
    for key, limit in (("num_qubits", MAX_INTERFACE_QUBITS), ("num_clbits", MAX_CLASSICAL_BITS)):
        width = ir[key]
        if type(width) is not int or width < 0:
            raise ValueError(f"Invalid {key} interface width")
        if width > limit:
            raise NotChecked(f"Offline interface allocation budget exceeded: {key}={width}, limit={limit}; no SDK object constructed")
    if len(ir.get("instructions", [])) > MAX_INSTRUCTIONS:
        raise NotChecked("Offline instruction allocation budget exceeded: limit=100000; no SDK object constructed")


def _submission_domain(ir, max_qubits):
    _interface_limits(ir)
    initial = ir.get("initial_logical_to_physical") or range(ir["num_qubits"])
    final = ir.get("logical_to_physical") or range(ir["num_qubits"])
    for mapping in (initial, final):
        if any(type(q) is not int or not 0 <= q < ir["num_qubits"] for q in mapping) or len(set(mapping)) != len(mapping):
            raise ValueError("Invalid physical qubit layout before SDK construction")
    for item in ir.get("instructions", []):
        for field, width in (("qubits", ir["num_qubits"]), ("clbits", ir["num_clbits"])):
            operands = item.get(field, [])
            if any(type(index) is not int or not 0 <= index < width for index in operands) or len(set(operands)) != len(operands):
                raise ValueError(f"Invalid {field} operands before SDK construction")
    logical = len(ir.get("quantum_inputs", initial))
    active = set(initial) | set(final) | {q for item in ir.get("instructions", []) for q in item.get("qubits", [])}
    if logical > max_qubits or len(active) > max_qubits + 2:
        raise NotChecked(f"Submission oracle limit exceeded before SDK construction: {logical} logical, {len(active)} active qubits")


def verify_artifact(artifact, *, max_qubits=4, max_paths=256, threshold=2e-10, store=True):
    from .engine import MAX_WORKING_BYTES, compare
    from .parsers import decode
    from .submission import construct, google_roundtrip, interpret
    evidence = {"schema_version": VERIFICATION_VERSION, "artifact_hash": artifact_hash(artifact),
                "logical_hash": digest(artifact.logical_ir), "physical_hash": digest(artifact.physical_ir),
                "program_hash": hashlib.sha256(artifact.program_bytes()).hexdigest(),
                "network_used": False, "certified": False, "whole_program_error": None,
                "tools": {"python": platform.python_version(), "oracle": "qstack-independent-1"},
                "limits": {"max_qubits": max_qubits, "max_paths": max_paths, "absolute_threshold": threshold,
                           "max_working_bytes": MAX_WORKING_BYTES},
                "checks": []}
    evidence["limits"].update(max_interface_qubits=MAX_INTERFACE_QUBITS,
        max_classical_bits=MAX_CLASSICAL_BITS, max_instructions=MAX_INSTRUCTIONS)
    for package in ("numpy", "pyqir", "heisenberg-spinor-submit", "qiskit", "cirq-core", "cirq-google",
                    "iqm-client", "iqm-pulse", "amazon-braket-sdk", "oqc-qcaas-client", "qibolab"):
        try:
            evidence["tools"][package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            pass
    evidence["tools"]["oracle_source_sha256"] = hashlib.sha256(b"".join(
        path.name.encode()+b"\0"+path.read_bytes() for path in sorted(Path(__file__).parent.glob("*.py")))).hexdigest()
    evidence["tools"]["submission_source_sha256"] = hashlib.sha256(b"".join(
        path.name.encode()+b"\0"+path.read_bytes() for path in sorted((Path(__file__).parent.parent/"providers").glob("*.py")))).hexdigest()
    submission = {}

    def actual_submission():
        if "value" not in submission:
            _submission_domain(artifact.physical_ir, max_qubits)
            for key in ("qubit_labels", "qubit_ids"):
                labels = artifact.target_snapshot.get(key)
                if labels is not None and len(labels) > MAX_INTERFACE_QUBITS:
                    raise NotChecked(f"Submission target {key} exceeds the offline interface allocation budget; no SDK object constructed")
            submission["value"] = construct(artifact)
        return submission["value"]

    pairs = [("logical_to_physical", artifact.logical_ir, lambda: (artifact.physical_ir, [])),
             ("physical_to_program", artifact.physical_ir, lambda: decode(artifact))]
    if artifact.route != "local":
        pairs.append(("physical_to_submission", artifact.physical_ir,
                      lambda: interpret(artifact, actual_submission())))
    if artifact.route == "google":
        pairs.append(("submission_to_engine_protobuf", lambda: interpret(artifact, actual_submission())[0],
                      lambda: google_roundtrip(artifact, actual_submission())))
    if artifact.route == "qibolab":
        def unavailable_pulses():
            raise NotChecked("Calibrated Qibolab pulses cannot be inferred from the artifact's calibration fingerprint; "
                "offline verification never loads a platform factory or connects instruments. Only the shared native assembler plan is checked")
        pairs.append(("submission_to_calibrated_pulses", artifact.physical_ir, unavailable_pulses))
    for name, before, after in pairs:
        check = {"name": name}
        if name == "physical_to_submission":
            check.update(route=artifact.route, scope="actual shared pure submission builder; no service client",
                         execution_limits=["Authentication, provider processing, pulse calibration and hardware execution are not checked"])
        elif name == "submission_to_engine_protobuf":
            check.update(parameter_precision="Engine protobuf float32 gate arguments", threshold_policy="user threshold unchanged")
        try:
            if callable(before):
                before = before()
            if before is None:
                raise NotChecked("Logical IR is unavailable in this v1 artifact; recompile for source-to-physical verification")
            _interface_limits(before)
            from ..classical import validate_classical
            validate_classical(before)
            decoded, gaps = after()
            _interface_limits(decoded)
            check.update(compare(before, decoded, max_qubits=max_qubits, max_paths=max_paths, threshold=threshold,
                                 allow_scalar_phase_loss=bool(gaps)))
            check["coverage_limits"] = gaps
        except (NotChecked, ImportError) as exc:
            check.update(status="not_checked", reason=str(exc), coverage_limits=[str(exc)])
        except QStackError as exc:
            status = "not_checked" if exc.code in {"MISSING_DEPENDENCY", "MISSING_TARGET_SNAPSHOT", "UNSUPPORTED_CAPABILITY", "UNSUPPORTED_GATE", "UNSUPPORTED_FORMAT"} else "failed"
            check.update(status=status, reason=str(exc), code=exc.code, coverage_limits=[str(exc)])
        except (ValueError, KeyError, IndexError, TypeError, AttributeError, SyntaxError, RuntimeError) as exc:
            check.update(status="failed", reason=f"Malformed program: {exc}")
        evidence["checks"].append(check)
    statuses = {c["status"] for c in evidence["checks"]}
    evidence["status"] = "failed" if "failed" in statuses else "not_checked" if "not_checked" in statuses else "passed"
    evidence["coverage_complete"] = evidence["status"] == "passed" and all(not c.get("coverage_limits") for c in evidence["checks"])
    if store and artifact.path:
        directory = Path(artifact.path) / "verification"
        directory.mkdir(exist_ok=True)
        path = directory / (digest(evidence) + ".json")
        path.write_bytes(canonical_json(evidence))
        evidence["evidence_path"] = str(path)
    return evidence
