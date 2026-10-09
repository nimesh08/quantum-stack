"""Offline independent finite-program verification; no provider or compiler calls."""
from __future__ import annotations

import hashlib
import importlib.metadata
import platform
from pathlib import Path

from ..artifacts import artifact_hash
from ..models import VERIFICATION_VERSION
from ..registry import canonical_json, digest


class NotChecked(Exception):
    """The requested program exceeds this independent oracle's declared domain."""


def verify_artifact(artifact, *, max_qubits=4, max_paths=256, threshold=2e-10, store=True):
    from .engine import MAX_WORKING_BYTES, compare
    from .parsers import decode
    evidence = {"schema_version": VERIFICATION_VERSION, "artifact_hash": artifact_hash(artifact),
                "logical_hash": digest(artifact.logical_ir), "physical_hash": digest(artifact.physical_ir),
                "program_hash": hashlib.sha256(artifact.program_bytes()).hexdigest(),
                "network_used": False, "certified": False, "whole_program_error": None,
                "tools": {"python": platform.python_version(), "oracle": "qstack-independent-1"},
                "limits": {"max_qubits": max_qubits, "max_paths": max_paths, "absolute_threshold": threshold,
                           "max_working_bytes": MAX_WORKING_BYTES},
                "checks": []}
    for package in ("numpy", "pyqir", "heisenberg-spinor-submit"):
        try:
            evidence["tools"][package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            pass
    evidence["tools"]["oracle_source_sha256"] = hashlib.sha256(b"".join(
        path.name.encode()+b"\0"+path.read_bytes() for path in sorted(Path(__file__).parent.glob("*.py")))).hexdigest()
    pairs = [("logical_to_physical", artifact.logical_ir, lambda: (artifact.physical_ir, [])),
             ("physical_to_program", artifact.physical_ir, lambda: decode(artifact))]
    for name, before, after in pairs:
        check = {"name": name}
        try:
            if before is None:
                raise NotChecked("Logical IR is unavailable in this v1 artifact; recompile for source-to-physical verification")
            from ..classical import validate_classical
            validate_classical(before)
            decoded, gaps = after()
            check.update(compare(before, decoded, max_qubits=max_qubits, max_paths=max_paths, threshold=threshold,
                                 allow_scalar_phase_loss=bool(gaps)))
            check["coverage_limits"] = gaps
        except (NotChecked, ImportError) as exc:
            check.update(status="not_checked", reason=str(exc), coverage_limits=[str(exc)])
        except (ValueError, KeyError, IndexError, TypeError, SyntaxError, RuntimeError) as exc:
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
