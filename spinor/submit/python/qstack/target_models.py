"""Validate supplied target quality/resource data independently of capabilities.

Missing measurements remain absent. A digest binds data to the exact recorded
inputs; it neither certifies a vendor nor predicts experimental fidelity.
"""
from __future__ import annotations
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path

from .models import QStackError, TARGET_SNAPSHOT_VERSION

_CAPABILITY_KEYS = (
    "route", "vendor", "device", "qubits", "native_gates", "gate_loci", "coupling", "coupling_map",
    "all_to_all", "directed_connectivity", "computational_qubits", "resonator_qubits", "qubit_labels",
    "available_qubits", "unavailable_qubits", "parameter_units", "parameter_bounds", "supports",
    "capabilities", "formats", "qir_platform", "capability_verified", "capability_verification",
    "capability_contract_sha256", "calibration_sha256", "calibration_set_id",
)
_QUALITY_KEYS = ("calibration", "scheduling", "calibration_aggregation", "timing_model_provenance")
_CALIBRATION_FIELDS = ("one_qubit_errors", "readout_errors", "two_qubit_errors", "instruction_durations", "operation_errors")


def _error(message):
    raise QStackError(message, "INVALID_TARGET_MODEL")


def _number(value, label, maximum=None):
    if type(value) not in {int, float} or not math.isfinite(value) or value < 0 or (maximum is not None and value > maximum):
        _error(f"{label} must be a finite nonnegative number" + (f" no greater than {maximum}" if maximum is not None else ""))


def _qubits(value, count, *, arity=None):
    if not isinstance(value, list) or (arity is not None and len(value) != arity) or any(
            type(q) is not int or q < 0 or q >= count for q in value) or len(set(value)) != len(value):
        _error("Target model contains invalid, duplicate or out-of-range physical qubits")
    return tuple(value)


def _provenance(value):
    if not isinstance(value, dict) or value.get("kind") not in {"vendor-sdk", "operator-file", "laboratory-config", "legacy-supplied"}:
        _error("Model provenance requires an explicit source kind")
    if not isinstance(value.get("source"), str) or not value["source"].strip():
        _error("Model provenance requires a nonempty source reference")
    stamps = {}
    for field in ("observed_at", "valid_until"):
        if field in value:
            try:
                stamp = datetime.fromisoformat(value[field].replace("Z", "+00:00"))
                if stamp.tzinfo is None:
                    raise ValueError
                stamps[field] = stamp
            except (ValueError, TypeError, AttributeError):
                _error(f"Model provenance {field} requires a timezone-qualified ISO timestamp")
    if "observed_at" in stamps and "valid_until" in stamps and stamps["valid_until"] < stamps["observed_at"]:
        _error("Model provenance valid_until precedes observed_at")


def normalize_target_model(record: dict) -> dict:
    result = deepcopy(record)
    if not isinstance(result.get("calibration", {}), dict):
        _error("calibration must be an object")
    calibration = dict(result.get("calibration", {}))
    for key in _CALIBRATION_FIELDS:
        if key in result:
            calibration[key] = result[key]
    count = result.get("qubits", 0)
    if count is None and not result.get("capability_verified"):
        count = 0
    if type(count) is not int or count < 0:
        _error("Target model requires an integer physical qubit count")
    for key in _CALIBRATION_FIELDS:
        if key in calibration and not isinstance(calibration[key], list):
            _error(f"{key} must be a list")
    for key, arity in (("one_qubit_errors", 1), ("readout_errors", 1), ("two_qubit_errors", 2)):
        seen = set()
        for row in calibration.get(key, []):
            if not isinstance(row, list) or len(row) != arity + 1:
                _error(f"{key} requires physical indices followed by an error probability")
            locus = _qubits(row[:-1], count, arity=arity)
            _number(row[-1], key, 1)
            if locus in seen:
                _error(f"Duplicate {key} locus")
            seen.add(locus)
    for key, value_field in (("instruction_durations", "duration_ns"), ("operation_errors", "error")):
        seen = set()
        for entry in calibration.get(key, []):
            if not isinstance(entry, dict) or not isinstance(entry.get("op"), str) or not entry["op"]:
                _error(f"{key} requires named operations")
            locus = _qubits(entry.get("qubits"), count)
            if "parameters" in entry and (not isinstance(entry["parameters"], list) or any(
                    type(v) not in {int, float} or not math.isfinite(v) for v in entry["parameters"])):
                _error("Calibration parameter applicability must contain finite numbers")
            identity = (entry["op"], locus, tuple(entry.get("parameters", [])))
            if identity in seen:
                _error(f"Duplicate {key} operation/locus")
            seen.add(identity)
            _number(entry.get(value_field), value_field, 1 if value_field == "error" else None)
            if key == "operation_errors" and entry.get("metric") not in {"average-gate-infidelity", "readout-error-probability", "reported-operation-error"}:
                _error("Operation errors require an explicit metric; uncertainty is not an operation error")
            if "provenance" in entry:
                _provenance(entry["provenance"])
    if "provenance" in calibration:
        _provenance(calibration["provenance"])
    if calibration:
        result["calibration"] = calibration
    scheduling = result.get("scheduling", {})
    if not isinstance(scheduling, dict):
        _error("scheduling must be an object")
    for key in ("exclusive_qubit_groups", "resources", "instruction_resources"):
        if key in scheduling and not isinstance(scheduling[key], list):
            _error(f"Scheduling {key} must be a list")
    if "feedback_latency_ns" in scheduling:
        _number(scheduling["feedback_latency_ns"], "feedback_latency_ns")
    for group in scheduling.get("exclusive_qubit_groups", []):
        if not _qubits(group, count):
            _error("Exclusive resource groups cannot be empty")
    resources = set()
    for resource in scheduling.get("resources", []):
        if not isinstance(resource, dict) or not isinstance(resource.get("id"), str) or not resource["id"] or resource["id"] in resources:
            _error("Scheduling resources require unique nonempty IDs")
        if resource.get("capacity", 1) != 1 or isinstance(resource.get("capacity", 1), bool):
            _error("Only exclusive capacity-one scheduling resources are currently supported")
        resources.add(resource["id"])
    seen = set()
    for entry in scheduling.get("instruction_resources", []):
        if not isinstance(entry, dict) or not isinstance(entry.get("op"), str):
            _error("Instruction resources require an operation name")
        identity = (entry["op"], _qubits(entry.get("qubits"), count))
        if identity in seen:
            _error("Duplicate operation/locus resource assignment")
        seen.add(identity)
        names = entry.get("resources")
        if not isinstance(names, list) or any(not isinstance(name, str) or name not in resources for name in names) or len(set(names)) != len(names):
            _error("Instruction refers to an unknown or duplicate scheduling resource")
    if "provenance" in scheduling:
        _provenance(scheduling["provenance"])
    return result


def target_fingerprints(record: dict) -> dict:
    normalized = normalize_target_model(record)
    def fingerprint(keys):
        data = {key: normalized[key] for key in keys if key in normalized}
        return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    return {"capability_hash": fingerprint(_CAPABILITY_KEYS), "quality_hash": fingerprint(_QUALITY_KEYS)}


def model_validity(record: dict, *, as_of: datetime | str | None = None) -> dict:
    """Assess declared time bounds; this does not certify the supplied source."""
    normalized = normalize_target_model(record)
    try:
        moment = datetime.now(timezone.utc) if as_of is None else (
            datetime.fromisoformat(as_of.replace("Z", "+00:00")) if isinstance(as_of, str) else as_of)
        if not isinstance(moment, datetime) or moment.tzinfo is None:
            raise ValueError
        moment = moment.astimezone(timezone.utc)
    except (ValueError, TypeError, AttributeError):
        _error("Model validity as_of requires a timezone-qualified timestamp")
    supplied = []
    for name, container in (("calibration", normalized.get("calibration", {})),
                            ("scheduling", normalized.get("scheduling", {}))):
        if "provenance" in container:
            supplied.append((name, container["provenance"]))
    if "timing_model_provenance" in normalized:
        _provenance(normalized["timing_model_provenance"])
        supplied.append(("timing_model", normalized["timing_model_provenance"]))
    for field in ("instruction_durations", "operation_errors"):
        for index, entry in enumerate(normalized.get("calibration", {}).get(field, [])):
            if "provenance" in entry:
                supplied.append((f"calibration.{field}[{index}]", entry["provenance"]))
    sources = []
    for path, provenance in supplied:
        observed = datetime.fromisoformat(provenance["observed_at"].replace("Z", "+00:00")) if "observed_at" in provenance else None
        expiry = datetime.fromisoformat(provenance["valid_until"].replace("Z", "+00:00")) if "valid_until" in provenance else None
        status = ("not_yet_observed" if observed is not None and moment < observed else
                  "expired" if expiry is not None and moment > expiry else
                  "within_declared_interval" if expiry is not None else "unknown")
        sources.append({"path": path, "source": provenance["source"], "status": status,
                        "observed_at": provenance.get("observed_at"), "valid_until": provenance.get("valid_until")})
    states = {source["status"] for source in sources}
    status = ("expired" if "expired" in states else "not_yet_observed" if "not_yet_observed" in states else
              "within_declared_interval" if states == {"within_declared_interval"} else "unknown")
    return {"as_of": moment.isoformat(), "status": status, "sources": sources,
            "supplied_source_certified": False}


def attach_timing_model(record: dict, path: str | Path) -> dict:
    try:
        supplied = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        _error("Cannot read timing-model JSON")
    if not isinstance(supplied, dict) or supplied.get("schema_version") != 1:
        _error("Timing model must use schema_version 1")
    if supplied.get("route") != record.get("route") or supplied.get("device") != record.get("device"):
        _error("Timing model must match the exact route and device")
    if supplied.get("capability_hash") != target_fingerprints(record)["capability_hash"]:
        _error("Timing model capability binding differs; refresh the model")
    _provenance(supplied.get("provenance"))
    result = deepcopy(record)
    if "calibration" in supplied:
        if not isinstance(supplied["calibration"], dict):
            _error("Timing model calibration must be an object")
        result.setdefault("calibration", {}).update(supplied["calibration"])
        # Top-level legacy aliases must not override an explicit bound model.
        for key in _CALIBRATION_FIELDS:
            if key in supplied["calibration"]:
                result.pop(key, None)
    if "scheduling" in supplied:
        result["scheduling"] = supplied["scheduling"]
    result["timing_model_provenance"] = supplied["provenance"]
    result = normalize_target_model(result)
    result.update(target_fingerprints(result))
    return result
