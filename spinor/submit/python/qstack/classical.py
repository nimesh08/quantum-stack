"""Exact, typed classical IR and conservative device/format feature contracts."""
from __future__ import annotations

import re
from .models import QStackError

CLASSICAL_OPS = frozenset("c_const c_copy c_not c_and c_or c_xor c_add c_sub c_eq c_ne c_lt c_le c_gt c_ge c_shl c_shr c_cast c_select".split())
COMPARISONS = frozenset("c_eq c_ne c_lt c_le c_gt c_ge".split())


def exact_integer(value, *, width=64):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise QStackError("Wide integer attributes must use canonical unsigned decimal strings", "ARTIFACT_INVALID")
    result = int(value)
    if result >= 1 << width:
        raise QStackError(f"Integer attribute does not fit uint[{width}]", "ARTIFACT_INVALID")
    return result


def _type(value):
    width = value.get("width")
    if value.get("type") not in {"bool", "uint"} or type(width) is not int or not 1 <= width <= 64:
        raise QStackError("Classical values require bool or uint[1..64]", "ARTIFACT_INVALID")
    if value["type"] == "bool" and width != 1:
        raise QStackError("Boolean values must have width one", "ARTIFACT_INVALID")
    return value["type"], width


def validate_classical(ir):
    """Check declarations, exact literals, SSA producers and immutable outputs.

    Compiler control-flow validation proves path-sensitive definite assignment;
    this transport validator additionally rejects missing and duplicate producers.
    """
    if ir.get("schema_version", 1) == 1:
        if any(key in ir for key in ("classical_values", "classical_storage", "classical_outputs")):
            raise QStackError("Classical v2 metadata cannot be interpreted as v1 IR; recompile", "ARTIFACT_INVALID")
        return
    if ir.get("schema_version") != 2:
        raise QStackError("Unsupported physical IR version; recompile", "ARTIFACT_INVALID")
    width = ir.get("num_clbits", 0)
    values, produced, slots = {}, set(), set()
    for storage in ir.get("classical_storage", []):
        bits = storage.get("bits", [])
        if not bits or len(bits) != storage.get("width") or any(type(b) is not int or not 0 <= b < width for b in bits):
            raise QStackError("Invalid classical storage mapping", "ARTIFACT_INVALID")
        if slots.intersection(bits) or len(set(bits)) != len(bits):
            raise QStackError("Classical storage entries overlap", "ARTIFACT_INVALID")
        slots.update(bits)
        if storage.get("initialized"):
            exact_integer(storage.get("initial_value"), width=len(bits))
    for value in ir.get("classical_values", []):
        name = value.get("id")
        if not isinstance(name, str) or not name or name in values:
            raise QStackError("Invalid or duplicate classical value identifier", "ARTIFACT_INVALID")
        _, size = _type(value)
        bits = value.get("storage", [])
        if len(bits) != size or len(set(bits)) != size or any(type(b) is not int or not 0 <= b < width for b in bits):
            raise QStackError("Invalid classical value storage mapping", "ARTIFACT_INVALID")
        if value.get("visibility") not in {"private", "exported"}:
            raise QStackError("Classical visibility must be private or exported", "ARTIFACT_INVALID")
        values[name] = value
        if value.get("initialized"):
            exact_integer(value.get("initial_value"), width=size)
            produced.add(name)
    for inst in ir.get("instructions", []):
        op = inst.get("op")
        if op not in CLASSICAL_OPS and not (op == "measure" and "result" in inst):
            if "condition" in inst and inst["condition"] not in values:
                raise QStackError("Unknown immutable branch predicate", "ARTIFACT_INVALID")
            continue
        name = inst.get("result")
        if name not in values or name in produced:
            raise QStackError("Classical SSA result needs one declared, unique producer", "ARTIFACT_INVALID")
        expected = 0 if op in {"c_const", "measure"} else 3 if op == "c_select" else 1 if op in {"c_copy", "c_not", "c_cast"} else 2
        inputs = inst.get("inputs", [])
        if len(inputs) != expected or any(v not in produced for v in inputs):
            raise QStackError("Classical operation has invalid or undefined inputs", "ARTIFACT_INVALID")
        if op == "c_const":
            exact_integer(inst.get("value"), width=values[name]["width"])
        if op in COMPARISONS and _type(values[name]) != ("bool", 1):
            raise QStackError("Comparison result must be Boolean", "ARTIFACT_INVALID")
        if op not in {"c_cast", "c_select", "c_const", "measure"}:
            types = {_type(values[i]) for i in inputs}
            if len(types) > 1 or (op not in COMPARISONS and types != {_type(values[name])}):
                raise QStackError("Classical operand types differ; use an explicit cast", "ARTIFACT_INVALID")
        if op == "c_select" and (_type(values[inputs[0]]) != ("bool", 1) or any(_type(values[i]) != _type(values[name]) for i in inputs[1:])):
            raise QStackError("Classical join requires a Boolean predicate and matching arm types", "ARTIFACT_INVALID")
        produced.add(name)
    for output in ir.get("classical_outputs", []):
        name = output.get("value")
        if name not in produced or _type(output) != _type(values[name]):
            raise QStackError("Export references an undefined or mismatched classical value", "ARTIFACT_INVALID")
    exported = ir.get("exported_clbits", list(range(width)))
    if len(set(exported)) != len(exported) or any(type(b) is not int or not 0 <= b < width for b in exported):
        raise QStackError("Invalid exported readout mapping", "ARTIFACT_INVALID")
    _definite_assignment(ir, values)


def _definite_assignment(ir, declarations):
    """SSA definitions carry their dominating branch predicates.

    Select checks only its chosen arm. This avoids enumerating exponentially many
    shot paths while rejecting values that are undefined on any reachable arm.
    """
    definitions = {v["id"]: frozenset() for v in declarations.values() if v.get("initialized")}
    owners, stack, guards = {}, [], frozenset()
    def require(name, context):
        if name not in definitions or not definitions[name] <= context:
            raise QStackError(f"Classical value '{name}' is not definitely assigned on this control path", "ARTIFACT_INVALID")
    for index, item in enumerate(ir.get("instructions", [])):
        op = item["op"]
        if op == "if":
            predicate = item.get("condition") or owners.get(item.get("clbits", [None])[0], f"branch@{index}")
            if predicate in declarations: require(predicate, guards)
            truth = item.get("condition_value", 1)
            stack.append((guards, predicate, truth, dict(owners)))
            guards = guards | {(predicate, truth)}
            continue
        if op == "else":
            if not stack: raise QStackError("Unmatched classical branch", "ARTIFACT_INVALID")
            before, predicate, truth, old_owners = stack[-1]
            guards = before | {(predicate, 1-truth)}
            owners = dict(old_owners)
            continue
        if op == "endif":
            if not stack: raise QStackError("Unmatched classical branch", "ARTIFACT_INVALID")
            guards, _, _, owners = stack.pop()
            continue
        inputs = item.get("inputs", [])
        if op == "c_select":
            require(inputs[0], guards)
            require(inputs[1], guards | {(inputs[0], 1)})
            require(inputs[2], guards | {(inputs[0], 0)})
        else:
            for name in inputs: require(name, guards)
        result = item.get("result")
        if result:
            definitions[result] = guards
            if declarations[result]["width"] == 1:
                owners[declarations[result]["storage"][0]] = result
    if stack: raise QStackError("Unclosed classical branch", "ARTIFACT_INVALID")
    for output in ir.get("classical_outputs", []): require(output["value"], frozenset())


def extract_requirements(ir):
    features, widths = set(), set()
    values = {v["id"]: v for v in ir.get("classical_values", [])}
    widths.update(v["width"] for v in values.values() if v["type"] == "uint")
    for item in ir.get("instructions", []):
        op = item["op"]
        if op in CLASSICAL_OPS:
            feature = "classical." + op[2:]
            features.add(feature)
            if values[item["result"]]["type"] == "uint":
                widths.add(values[item["result"]]["width"])
        if op == "if":
            features.add("branching.bit")
        if op in {"measure", "reset"}:
            features.add(op)
    if any(o.get("role") == "loop_exhausted" for o in ir.get("classical_outputs", [])):
        features.add("output.loop_exhausted")
    if ir.get("classical_outputs"):
        features.add("output.classical")
    return {"schema_version": 1, "features": sorted(features), "integer_widths": sorted(widths)}


def needs_controller_contract(requirements):
    return bool(requirements.get("integer_widths")) or any(
        feature.startswith(("classical.", "output.")) for feature in requirements.get("features", []))


def feature_support(record, name):
    features = record.get("capabilities", {}).get("features", {})
    explicit = features.get(name, "unknown")
    if explicit not in {"supported", "unsupported", "unknown"}:
        raise QStackError(f"Invalid three-state capability for {name}", "TARGET_INVALID")
    if name in features:
        return explicit
    legacy = record.get("supports", {})
    if name == "measure":
        return "supported"
    if name == "reset" and "reset" in legacy:
        return "supported" if legacy["reset"] else "unsupported"
    if name == "branching.bit" and "feedforward" in legacy:
        return "supported" if legacy["feedforward"] in {True, "full", "limited"} else "unsupported"
    # Legacy feedforward never grants arithmetic, copying or bounded execution.
    return "unknown"


def validate_requirements(requirements, device, serializer):
    for name in requirements.get("features", []):
        for label, contract in (("device", device), ("output format", serializer)):
            state = feature_support(contract, name)
            if state != "supported":
                raise QStackError(f"{label} capability '{name}' is {state}; select an explicitly validated device/output combination", "UNSUPPORTED_CAPABILITY")
    if requirements.get("integer_widths"):
        for label, contract in (("device", device), ("output format", serializer)):
            allowed = contract.get("capabilities", {}).get("integer_widths", [])
            if any(width not in allowed for width in requirements["integer_widths"]):
                raise QStackError(f"{label} does not explicitly support the requested integer widths", "UNSUPPORTED_CAPABILITY")


def serializer_capabilities(format):
    """Owned serializer contracts; route SDK admission is checked separately.

    A language's expressiveness cannot authorize an undiscovered controller.
    """
    basic = {"measure": "supported", "reset": "supported", "branching.bit": "supported"}
    if format in {"qasm3", "qir", "qir-text", "qir-bitcode", "json"}:
        basic.update({"classical." + op[2:]: "supported" for op in CLASSICAL_OPS})
        basic["output.loop_exhausted"] = "supported"
        basic["output.classical"] = "supported"
        return {"capabilities": {"features": basic, "integer_widths": list(range(1, 65))}}
    return {"capabilities": {"features": basic, "integer_widths": []}}
