"""Historical profiles and explicit, versioned discovery snapshots."""
from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import yaml

from .config import state_path
from .models import QStackError
from .target_models import normalize_target_model, target_fingerprints, TARGET_SNAPSHOT_VERSION


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def digest(value: Any) -> str:
    return hashlib.sha256(canonical_json(value)).hexdigest()


def registry_root(config: dict | None = None) -> Path:
    explicit = (config or {}).get("registry") or os.environ.get("SPINOR_REGISTRY_ROOT")
    if explicit:
        root = Path(explicit).expanduser().resolve()
        if not (root / "chips").is_dir():
            raise QStackError(f"Registry has no chips directory: {root}")
        return root
    for parent in Path(__file__).resolve().parents:
        candidate = parent / "spinor" / "registry"
        if (candidate / "chips").is_dir():
            return candidate
    packaged = Path(__file__).parent / "data" / "registry"
    if (packaged / "chips").is_dir():
        return packaged
    raise QStackError("Registry not found; set --registry or SPINOR_REGISTRY_ROOT", "REGISTRY_MISSING")


def profiles(config: dict | None = None) -> list[dict]:
    root = registry_root(config)
    result = []
    for path in sorted((root / "chips").glob("*.yaml")):
        record = yaml.safe_load(path.read_text(encoding="utf-8"))
        record["profile_path"] = str(path)
        record.setdefault("readiness", "needs_refresh")
        result.append(record)
    return result


def snapshot_path(route: str, device: str) -> Path:
    return state_path() / "targets" / route / (digest(device)[:24] + ".json")


def cache_targets(route: str, records: list[dict]) -> list[dict]:
    saved = []
    for item in records:
        item = normalize_target_model(item)
        calibration = dict(item.get("calibration", {}))
        for key in ("one_qubit_errors", "readout_errors", "two_qubit_errors", "instruction_durations"):
            if key in item:
                calibration[key] = item[key]
        if calibration:
            item["calibration"] = calibration
        device = item.get("device") or item.get("id") or item.get("name")
        if not device:
            raise QStackError(f"{route} discovery omitted a device identifier", "PROVIDER_CONTRACT_ERROR")
        item.update(device=str(device), route=route, schema_version=TARGET_SNAPSHOT_VERSION,
                    retrieved_at=datetime.now(timezone.utc).isoformat())
        item.setdefault("capability_verified", False)
        item.setdefault("readiness", "discovered" if item["capability_verified"] else "needs_refresh")
        if route not in {"aws", "azure"}:
            item.setdefault("vendor", route)
        item.update(target_fingerprints(item))
        item["snapshot_hash"] = digest({k: v for k, v in item.items() if k not in {"retrieved_at", "snapshot_hash"}})
        path = snapshot_path(route, str(device))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(canonical_json(item))
        saved.append(item)
    return saved


def get_target(target: str, route: str | None = None, config: dict | None = None) -> dict:
    config = config or {}
    static = next((p for p in profiles(config) if p["id"] == target), None)
    if static is None and target == "generic":
        return {"id": "generic", "provider": "local", "vendor": "generic", "readiness": "offline_only",
                "qubits": 24, "all_to_all": True, "native_gates": ["h", "x", "y", "z", "s", "sdg", "t", "tdg", "rx", "ry", "rz", "cx", "cz", "swap", "sx", "sxdg", "gpi", "gpi2", "u1q", "ms", "rxx", "rzz"],
                "supports": {"mid_circuit_measure": True, "reset": True, "feedforward": "full"},
                "capability_verified": False, "routes": ["local"]}
    route = route or (static or {}).get("provider")
    device = config.get("device") or target
    if route and snapshot_path(route, device).is_file():
        discovered = json.loads(snapshot_path(route, device).read_text(encoding="utf-8"))
        merged = dict(static or {})
        merged.update(discovered, id=target, provider=route)
        if (static or {}).get("readiness") == "family" and discovered.get("capability_verified") and device != target:
            merged.update(readiness="discovered", readiness_reason="Family resolved to an authenticated concrete device.")
        return merged
    if static is None:
        raise QStackError(f"Unknown target '{target}'; refresh provider targets or select a registry profile", "UNKNOWN_TARGET")
    if route and route not in static.get("routes", [static["provider"]]) and route != "local":
        raise QStackError(f"Target '{target}' has no '{route}' route", "UNSUPPORTED_ROUTE")
    return static


def target_edges(record: dict, config: dict | None = None) -> tuple[bool, list[list[int]]]:
    if "coupling" in record:
        return bool(record.get("all_to_all", False)), record["coupling"]
    cm = record.get("coupling_map", {})
    name = cm.get("topology")
    if cm.get("all_to_all") or name == "all_to_all" or record.get("all_to_all"):
        return True, []
    if name == "linear_n":
        return False, [[i, i + 1] for i in range(int(record["qubits"]) - 1)]
    if name:
        path = registry_root(config) / "topologies" / f"{name}.yaml"
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        return bool(data.get("all_to_all", False)), data.get("edges", [])
    return False, []


def validate_physical(ir: dict, target: dict, config: dict | None = None) -> None:
    from .classical import CLASSICAL_OPS, validate_classical, feature_support
    validate_classical(ir)
    nq, nc = ir.get("num_qubits"), ir.get("num_clbits")
    if type(nq) is not int or type(nc) is not int or nq < 0 or nc < 0:
        raise QStackError("Invalid physical IR register sizes", "ARTIFACT_INVALID")
    if nq > int(target.get("qubits", nq)):
        raise QStackError("Program exceeds the target qubit capacity", "TARGET_INCOMPATIBLE")
    basis = set(target.get("native_gates", []))
    all_to_all, coupling = target_edges(target, config)
    edges = {tuple(e) for e in coupling}
    directed = target.get("directed_connectivity", False)
    measured = set()
    branches = []
    import math
    if not isinstance(ir.get("global_phase", 0), (int, float)) or not math.isfinite(ir.get("global_phase", 0)):
        raise QStackError("Invalid global phase", "ARTIFACT_INVALID")
    expected_mapping = [{"qubit": item.get("qubits", [None])[0] if item.get("qubits") else None,
                         "clbit": item.get("clbits", [None])[0] if item.get("clbits") else None}
                        for item in ir.get("instructions", []) if item.get("op") == "measure"]
    for inst in ir.get("instructions", []):
        op, qs, cs = inst.get("op"), inst.get("qubits", []), inst.get("clbits", [])
        if any(type(q) is not int or q < 0 or q >= nq for q in qs) or len(set(qs)) != len(qs):
            raise QStackError("Invalid physical qubit index", "ARTIFACT_INVALID")
        if any(type(c) is not int or c < 0 or c >= nc for c in cs):
            raise QStackError("Invalid classical bit index", "ARTIFACT_INVALID")
        if any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in inst.get("params", [])):
            raise QStackError("Unbound or non-finite gate parameter", "ARTIFACT_INVALID")
        if any(q in target.get("unavailable_qubits", []) for q in qs):
            raise QStackError("Program uses an unavailable physical qubit", "TARGET_INCOMPATIBLE")
        if "available_qubits" in target and any(q not in target["available_qubits"] for q in qs):
            raise QStackError("Program uses a physical qubit absent from the available component list", "TARGET_INCOMPATIBLE")
        if op in CLASSICAL_OPS:
            continue  # Typed SSA validation above owns classical operands.
        if op in {"if", "else", "endif"}:
            if qs or inst.get("params", []) or (op != "if" and cs):
                raise QStackError("Invalid operands on a control-flow marker", "ARTIFACT_INVALID")
            if feature_support(target, "branching.bit") != "supported":
                raise QStackError("Target does not explicitly support classical feedforward", "TARGET_INCOMPATIBLE")
            if op == "if":
                if ("condition" not in inst and len(cs) != 1) or inst.get("condition_value", 1) not in {0, 1}:
                    raise QStackError("Invalid classical condition", "ARTIFACT_INVALID")
                branches.append(False)
            elif not branches or (op == "else" and branches[-1]):
                raise QStackError("Unbalanced conditional region", "ARTIFACT_INVALID")
            elif op == "else":
                branches[-1] = True
            else:
                branches.pop()
            continue
        if op == "gphase":
            if qs or cs or len(inst.get("params", [])) != 1:
                raise QStackError("Invalid phase instruction", "ARTIFACT_INVALID")
            continue
        arities = {**{g: (1, {0}) for g in ("id", "h", "x", "y", "z", "s", "sdg", "t", "tdg", "sx", "sxdg", "reset", "measure")},
                   **{g: (1, {1}) for g in ("rx", "ry", "rz", "gpi", "gpi2")},
                   "u1q": (1, {2}), "phased_xz": (1, {3}),
                   **{g: (2, {0}) for g in ("cx", "cz", "ecr", "swap", "iswap", "sqrt_iswap", "sqrt_iswap_inv", "syc", "move")},
                   "ms": (2, {0, 2, 3}), "rxx": (2, {1}), "rzz": (2, {1})}
        if op != "barrier" and (op not in arities or len(qs) != arities[op][0] or len(inst.get("params", [])) not in arities[op][1]):
            raise QStackError(f"Invalid operand/parameter arity for '{op}'", "ARTIFACT_INVALID")
        if op != "measure" and cs:
            raise QStackError("Quantum gate has an unexpected classical destination", "ARTIFACT_INVALID")
        if op not in {"measure", "reset", "barrier"} and basis and op not in basis:
            raise QStackError(f"Gate '{op}' is not native to the target", "TARGET_INCOMPATIBLE")
        if op == "measure":
            if feature_support(target, "measure") != "supported":
                raise QStackError("Target does not support measurement", "TARGET_INCOMPATIBLE")
            if len(qs) != 1 or len(cs) != 1:
                raise QStackError("Measurement requires one qubit and one destination bit", "ARTIFACT_INVALID")
            measured.update(qs)
        elif op != "barrier" and any(q in measured for q in qs) and not target.get("supports", {}).get("mid_circuit_measure", False):
            raise QStackError("Target does not support operations after measurement", "TARGET_INCOMPATIBLE")
        if op == "reset" and feature_support(target, "reset") != "supported":
            raise QStackError("Target does not support reset", "TARGET_INCOMPATIBLE")
        asymmetric = op in {"cx", "ecr", "move"}
        if len(qs) == 2 and not all_to_all and tuple(qs) not in edges and ((directed and asymmetric) or tuple(reversed(qs)) not in edges):
            raise QStackError(f"Gate uses disconnected physical qubits {qs}", "TARGET_INCOMPATIBLE")
        loci = target.get("gate_loci", {}).get(op)
        if loci is not None and qs not in loci and (directed or asymmetric or list(reversed(qs)) not in loci):
            raise QStackError(f"Gate '{op}' is unavailable on physical qubits {qs}", "TARGET_INCOMPATIBLE")
    if branches:
        raise QStackError("Unclosed conditional region", "ARTIFACT_INVALID")
    if ir.get("measurement_mapping", []) != expected_mapping:
        raise QStackError("Measurement mapping differs from physical instructions", "ARTIFACT_INVALID")
    for key in ("logical_to_physical", "initial_logical_to_physical"):
        mapping = ir.get(key, [])
        if len(set(mapping)) != len(mapping) or any(type(q) is not int or q < 0 or q >= nq for q in mapping):
            raise QStackError("Invalid logical to physical qubit mapping", "ARTIFACT_INVALID")
        if any(q in target.get("unavailable_qubits", []) for q in mapping) or (
                "available_qubits" in target and any(q not in target["available_qubits"] for q in mapping)):
            raise QStackError("Logical layout includes an unavailable physical qubit", "TARGET_INCOMPATIBLE")
    if target.get("resonator_qubits") or any(i.get("op") == "move" for i in ir.get("instructions", [])):
        from .providers.iqm_contract import validate_moves
        validate_moves(ir, target)


def canonical_format(value: str) -> str:
    return {"openqasm2": "qasm2", "openqasm3": "qasm3", "braket-openqasm3": "qasm3",
            "qir": "qir-bitcode", "qir.bc": "qir-bitcode", "qir.v1": "qir-bitcode"}.get(value.lower(), value.lower())


def validate_format(route: str, program_format: str, target: dict) -> None:
    formats = {canonical_format(f) for f in target.get("formats", [])}
    actual = canonical_format(program_format)
    # IBM's text artifact is an export; native QuantumCircuit construction
    # consumes the owned physical IR, without importing/recompiling that text.
    if route == "ibm" and "qiskit-native" in formats and actual in {"qasm3", "json", "qiskit-native"}:
        return
    if not formats or actual not in formats:
        raise QStackError("Device no longer accepts the artifact program format; refresh and recompile", "TARGET_INCOMPATIBLE")


def ensure_live_target(record: dict) -> None:
    if record.get("readiness") in {"offline_only", "retired", "family", "unavailable"}:
        raise QStackError(record.get("readiness_reason", "Target is not live-ready"), "TARGET_UNAVAILABLE")
    if not record.get("capability_verified") or not record.get("device"):
        raise QStackError("Refresh and select a concrete provider device before live compilation/submission", "TARGET_REFRESH_REQUIRED")
