"""Deterministic dependency/resource scheduling, using measured durations only.

The schedule records independent operations and worst-case branch joins. Provider
services may retime an untimed program; this report never claims pulse timing.
"""
from __future__ import annotations

import math
from .classical import CLASSICAL_OPS
from .target_models import normalize_target_model, target_fingerprints, model_validity


def optimization_report(ir: dict, snapshot: dict, *, as_of=None) -> dict:
    # Standalone report callers may omit a target width; the physical IR supplies
    # its index space. This never creates a verified target capability record.
    snapshot = dict(snapshot)
    snapshot.setdefault("qubits", ir.get("num_qubits", max(
        (q for inst in ir.get("instructions", []) for q in inst.get("qubits", [])), default=-1) + 1))
    snapshot = normalize_target_model(snapshot)
    validity = model_validity(snapshot, as_of=as_of)
    times, layers, branch_stack, schedule = {}, {}, [], []
    durations = {}
    for entry in snapshot.get("calibration", snapshot).get("instruction_durations", []):
        value = entry.get("duration_ns")
        if isinstance(value, (int, float)) and math.isfinite(value) and value >= 0:
            durations[(entry["op"], tuple(entry["qubits"]),
                       tuple(entry["parameters"]) if "parameters" in entry else None)] = value
    resource_groups = [set(g) for g in snapshot.get("scheduling", {}).get("exclusive_qubit_groups", [])]
    instruction_resources = {(item["op"], tuple(item["qubits"])): item["resources"]
        for item in snapshot.get("scheduling", {}).get("instruction_resources", [])}
    value_storage = {entry["id"]: entry.get("storage", []) for entry in ir.get("classical_values", [])}
    feedback_latency = snapshot.get("scheduling", {}).get("feedback_latency_ns")
    if type(feedback_latency) not in {int, float} or not math.isfinite(feedback_latency) or feedback_latency < 0:
        feedback_latency = None
    gate_count = two_count = 0
    unknown = []
    untimed_feedback = []
    fence_time = fence_layer = 0

    def merge(left, right):
        return {k: max(left.get(k, 0), right.get(k, 0)) for k in left.keys() | right.keys()}

    for index, inst in enumerate(ir.get("instructions", [])):
        op, qs, cs = inst["op"], inst.get("qubits", []), inst.get("clbits", [])
        if op == "if":
            # Conservatively fence branch entry and join, including classical readout.
            t = max(fence_time, max(times.values(), default=0))
            if feedback_latency is None:
                untimed_feedback.append(index)
            else:
                t += feedback_latency
            layer = max(fence_layer, max(layers.values(), default=0))
            times = dict.fromkeys(times, t)
            layers = dict.fromkeys(layers, layer)
            fence_time, fence_layer = t, layer
            branch_stack.append((dict(times), dict(layers), t, layer, None, None))
            continue
        if op == "else":
            before_t, before_l, entry_t, entry_l, _, _ = branch_stack[-1]
            branch_stack[-1] = (before_t, before_l, entry_t, entry_l, times, layers)
            times, layers = dict(before_t), dict(before_l)
            fence_time, fence_layer = entry_t, entry_l
            continue
        if op == "endif":
            before_t, before_l, entry_t, entry_l, then_t, then_l = branch_stack.pop()
            times = merge(times, then_t if then_t is not None else before_t)
            layers = merge(layers, then_l if then_l is not None else before_l)
            fence_time = max(entry_t, max(times.values(), default=0))
            fence_layer = max(entry_l, max(layers.values(), default=0))
            continue
        resources = [*(f"q{q}" for q in qs), *(f"c{c}" for c in cs)]
        resources += [f"value:{value}" for value in inst.get("inputs", [])]
        if "result" in inst:
            resources.append(f"value:{inst['result']}")
        # Distinct SSA values may reuse a controller storage slot. Reads and
        # writes to that physical bit must still respect the program order.
        for value in [*inst.get("inputs", []), *([inst["result"]] if "result" in inst else [])]:
            resources += [f"c{bit}" for bit in value_storage.get(value, [])]
        resources += [f"group{n}" for n, group in enumerate(resource_groups) if group.intersection(qs)]
        resources += [f"resource:{name}" for name in instruction_resources.get((op, tuple(qs)), [])]
        if op == "barrier" and not qs:
            resources = sorted(times.keys() | layers.keys())
        start = max(fence_time, max((times.get(r, 0) for r in resources), default=0))
        layer = max(fence_layer, max((layers.get(r, 0) for r in resources), default=0))
        def lookup(wires):
            return durations.get((op, wires, tuple(inst.get("params", []))), durations.get((op, wires, None)))
        duration = lookup(tuple(qs))
        if op in {"barrier", "gphase"}:
            duration = 0
        elif duration is None:
            unknown.append(index)
        if op not in {"measure", "reset", "barrier", "gphase"} and op not in CLASSICAL_OPS:
            gate_count += 1
            two_count += len(qs) == 2
        for resource in resources:
            times[resource] = start + (duration or 0)
            layers[resource] = layer + (op not in {"barrier", "gphase"})
        if op == "barrier" and not qs:
            fence_time, fence_layer = start, layer
        schedule.append({"instruction": index, "layer": layer,
                         "start_ns": start, "duration_ns": duration, "resources": resources})
    # Partial timing is not a duration estimate: unknown gates can dominate it.
    incomplete = bool(unknown or untimed_feedback or validity["status"] in {"expired", "not_yet_observed"})
    if incomplete:
        for item in schedule:
            item["start_ns"] = None
    return {"gate_count": gate_count, "two_qubit_count": two_count,
            "depth": max(layers.values(), default=0),
            "duration_seconds": None if incomplete else max(fence_time, max(times.values(), default=0)) * 1e-9,
            "schedule": schedule, "timing_complete": not incomplete,
            "untimed_instructions": unknown, "schedule_policy": "resource-aware-asap",
            "untimed_feedback": untimed_feedback, "feedback_latency_ns": feedback_latency,
            "timing_assumptions": ["supplied gate durations and exclusive qubit groups", "worst-case branch joins",
                                   "feedback latency required for every runtime condition", "provider may retime untimed output"],
            "timing_enforced_by_provider": False,
            "model_fingerprints": target_fingerprints(snapshot),
            "model_validity_at_compile": validity,
            "calibration_provenance": snapshot.get("calibration", {}).get("provenance"),
            "timing_model_provenance": snapshot.get("timing_model_provenance", snapshot.get("scheduling", {}).get("provenance")),
            "branch_metric": "gate counts include both branches; depth/duration use worst-case joins"}
