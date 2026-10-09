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
        resources += [f"group{n}" for n, group in enumerate(resource_groups) if group.intersection(qs)]
        if op == "barrier" and not qs:
            resources = sorted(times.keys() | layers.keys())
        start = max(fence_time, max((times.get(r, 0) for r in resources), default=0))
        layer = max(fence_layer, max((layers.get(r, 0) for r in resources), default=0))
        duration = durations.get((op, tuple(qs)))
        if duration is None and len(qs) == 2 and op not in {"cx", "ecr", "move"}:
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
        if op == "barrier" and not qs:
            fence_time, fence_layer = start, layer
        schedule.append({"instruction": index, "layer": layer,
                         "start_ns": start, "duration_ns": duration, "resources": resources})
    # Partial timing is not a duration estimate: unknown gates can dominate it.
    incomplete = bool(unknown or untimed_feedback)
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
            "branch_metric": "gate counts include both branches; depth/duration use worst-case joins"}
