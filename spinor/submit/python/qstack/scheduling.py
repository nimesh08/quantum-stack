"""Deterministic dependency/resource scheduling, using measured durations only.

The schedule records independent operations and worst-case branch joins. Provider
services may retime an untimed program; this report never claims pulse timing.
"""
from __future__ import annotations

import math


def optimization_report(ir: dict, snapshot: dict) -> dict:
    times, layers, branch_stack, schedule = {}, {}, [], []
    durations = {}
    for entry in snapshot.get("calibration", snapshot).get("instruction_durations", []):
        value = entry.get("duration_ns")
        if isinstance(value, (int, float)) and math.isfinite(value) and value >= 0:
            durations[(entry["op"], tuple(entry["qubits"]))] = value
    resource_groups = [set(g) for g in snapshot.get("scheduling", {}).get("exclusive_qubit_groups", [])]
    gate_count = two_count = 0
    unknown = []

    def merge(left, right):
        return {k: max(left.get(k, 0), right.get(k, 0)) for k in left.keys() | right.keys()}

    for index, inst in enumerate(ir.get("instructions", [])):
        op, qs, cs = inst["op"], inst.get("qubits", []), inst.get("clbits", [])
        if op == "if":
            # Conservatively fence branch entry and join, including classical readout.
            t, layer = max(times.values(), default=0), max(layers.values(), default=0)
            times = dict.fromkeys(times, t)
            layers = dict.fromkeys(layers, layer)
            branch_stack.append((dict(times), dict(layers), None, None))
            continue
        if op == "else":
            before_t, before_l, _, _ = branch_stack[-1]
            branch_stack[-1] = (before_t, before_l, times, layers)
            times, layers = dict(before_t), dict(before_l)
            continue
        if op == "endif":
            before_t, before_l, then_t, then_l = branch_stack.pop()
            times = merge(times, then_t if then_t is not None else before_t)
            layers = merge(layers, then_l if then_l is not None else before_l)
            continue
        resources = [*(f"q{q}" for q in qs), *(f"c{c}" for c in cs)]
        resources += [f"group{n}" for n, group in enumerate(resource_groups) if group.intersection(qs)]
        start = max((times.get(r, 0) for r in resources), default=0)
        layer = max((layers.get(r, 0) for r in resources), default=0)
        duration = durations.get((op, tuple(qs)))
        if duration is None and len(qs) == 2 and op not in {"cx", "ecr"}:
            duration = durations.get((op, tuple(reversed(qs))))
        if op in {"barrier", "gphase"}:
            duration = 0
        elif duration is None:
            unknown.append(index)
        if op not in {"measure", "reset", "barrier", "gphase"}:
            gate_count += 1
            two_count += len(qs) == 2
        for resource in resources:
            times[resource] = start + (duration or 0)
            layers[resource] = layer + (op not in {"barrier", "gphase"})
        schedule.append({"instruction": index, "layer": layer,
                         "start_ns": start, "duration_ns": duration, "resources": resources})
    # Partial timing is not a duration estimate: unknown gates can dominate it.
    if unknown:
        for item in schedule:
            item["start_ns"] = None
    return {"gate_count": gate_count, "two_qubit_count": two_count,
            "depth": max(layers.values(), default=0),
            "duration_seconds": None if unknown else max(times.values(), default=0) * 1e-9,
            "schedule": schedule, "timing_complete": not unknown,
            "untimed_instructions": unknown, "schedule_policy": "resource-aware-asap",
            "timing_enforced_by_provider": False,
            "branch_metric": "gate counts include both branches; depth/duration use worst-case joins"}
