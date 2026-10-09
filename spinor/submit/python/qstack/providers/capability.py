"""Explicit account-provided capability contracts for opaque device APIs."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from qstack.models import QStackError
from .base import plain


def calibration_digest(calibration):
    data = json.dumps(plain(calibration), sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(data.encode("utf-8")).hexdigest()


def account_capability(path, *, route, device, calibration):
    """Bind an operator's explicit contract to the current account calibration.

This attests source/provenance, not independent hardware verification. It never
derives a gate set or topology from unrelated benchmarking metrics.
"""
    try:
        record = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        raise QStackError("Cannot read capability_snapshot JSON", "INVALID_CAPABILITY_SNAPSHOT") from None
    if not isinstance(record, dict) or record.get("schema_version") != 1:
        raise QStackError("Capability contract must use schema_version 1", "INVALID_CAPABILITY_SNAPSHOT")
    if record.get("route") != route or record.get("device") != device:
        raise QStackError("Capability contract does not match the exact provider route and QPU ID", "TARGET_INCOMPATIBLE")
    if record.get("calibration_sha256") != calibration_digest(calibration):
        raise QStackError("QPU calibration changed or does not match the capability contract; refresh the account-provided contract", "TARGET_INCOMPATIBLE")
    sources = record.get("capability_sources")
    if record.get("attestation") != "account-provided" or not isinstance(sources, list) or not sources or not all(isinstance(s, str) and s.strip() for s in sources):
        raise QStackError("Capability contract requires account-provided attestation and source references", "INVALID_CAPABILITY_SNAPSHOT")
    count = record.get("qubits")
    gates = record.get("native_gates")
    if not isinstance(count, int) or isinstance(count, bool) or count < 1 or not isinstance(gates, list) or not gates or not all(isinstance(g, str) and g for g in gates):
        raise QStackError("Capability contract requires a positive qubit count and explicit native_gates", "INVALID_CAPABILITY_SNAPSHOT")
    if record.get("parameter_units") != "radians" or not all(isinstance(record.get(k), bool) for k in ("all_to_all", "directed_connectivity")):
        raise QStackError("Capability contract must specify radians and explicit topology flags", "INVALID_CAPABILITY_SNAPSHOT")
    supports = record.get("supports", {})
    if not isinstance(supports, dict) or not all(isinstance(supports.get(k), bool) for k in ("reset", "mid_circuit_measure", "feedforward")):
        raise QStackError("Capability contract must specify all dynamic capability flags", "INVALID_CAPABILITY_SNAPSHOT")
    edges = record.get("coupling")
    if not isinstance(edges, list) or any(not isinstance(edge, list) or len(edge) != 2 or
        any(not isinstance(q, int) or isinstance(q, bool) or not 0 <= q < count for q in edge) or edge[0] == edge[1] for edge in edges):
        raise QStackError("Capability contract has invalid physical coupling edges", "INVALID_CAPABILITY_SNAPSHOT")
    if not isinstance(record.get("formats"), list) or not record["formats"] or not all(isinstance(fmt, str) and fmt for fmt in record["formats"]):
        raise QStackError("Capability contract requires explicit accepted formats", "INVALID_CAPABILITY_SNAPSHOT")
    result = {k: record[k] for k in ("qubits", "native_gates", "coupling", "all_to_all", "directed_connectivity",
        "supports", "formats", "parameter_units", "capability_sources", "calibration_sha256")}
    for name in ("gate_loci", "available_qubits", "unavailable_qubits", "qubit_labels", "one_qubit_errors",
                 "two_qubit_errors", "readout_errors", "instruction_durations"):
        if name in record:
            result[name] = record[name]
    result.update(capability_verified=True, capability_verification="account-provided-calibration-bound",
                  capability_contract_sha256=calibration_digest(record))
    return result
