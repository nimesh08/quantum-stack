"""Native Quantinuum HQSLIB OpenQASM 2 export, with no SDK compiler.

Contract: Quantinuum's hqslib1.inc declares U1q(theta,phi), Rz(lambda),
RZZ(theta), with angles in radians. Its dialect permits single-bit conditions.
https://github.com/Quantinuum/tket/blob/main/pytket/pytket/qasm/includes/hqslib1.inc
https://docs.quantinuum.com/tket/api-docs/_modules/pytket/qasm/qasm.html

QASM2 cannot encode scalar phase. The unchanged physical IR retains both global
and branch-local phases; comments identify them in this sampling-only export.
"""
from __future__ import annotations

from math import isfinite

from qstack.models import QStackError
from .native import instructions


def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not isfinite(value):
        raise QStackError("QASM2 parameters must be finite numbers", "ARTIFACT_INVALID")
    return format(value, ".17g")


def serialize_qasm2(ir: dict, snapshot: dict | None = None) -> str:
    """Preserve native gates, physical indices and c[n] readout destinations.

Simple if/else blocks become individual HQSLIB guarded operations. A branch
cannot change its own predicate: re-testing a changed bit for each statement
would differ from the IR's single branch-entry test. Nested conditions and
conditional barriers are rejected instead of weakening their meaning.
"""
    snapshot = snapshot or {}
    vendor = snapshot.get("vendor") or snapshot.get("provider") or snapshot.get("route")
    if snapshot and vendor != "quantinuum":
        raise QStackError("Native HQSLIB QASM2 export requires a Quantinuum target", "UNSUPPORTED_FORMAT")
    nq, nc = ir.get("num_qubits"), ir.get("num_clbits")
    if type(nq) is not int or type(nc) is not int or nq < 0 or nc < 0:
        raise QStackError("QASM2 requires nonnegative integer register sizes", "ARTIFACT_INVALID")
    lines = ['OPENQASM 2.0;', 'include "hqslib1.inc";']
    if nq:
        lines.append(f"qreg q[{nq}];")
    if nc:
        lines.append(f"creg c[{nc}];")
    phase = _number(ir.get("global_phase", 0))
    lines.append(f"// Scalar phase retained in physical IR: {phase} radians.")
    branch = None
    for inst in instructions(ir, allow_control=True):
        op, qs, cs, params = inst["op"].lower(), inst.get("qubits", []), inst.get("clbits", []), inst.get("params", [])
        if any(type(q) is not int or not 0 <= q < nq for q in qs) or len(set(qs)) != len(qs):
            raise QStackError("Invalid QASM2 physical qubit index", "ARTIFACT_INVALID")
        if any(type(c) is not int or not 0 <= c < nc for c in cs):
            raise QStackError("Invalid QASM2 classical bit index", "ARTIFACT_INVALID")
        numbers = [_number(value) for value in params]
        if op == "if":
            if branch is not None:
                raise QStackError("Nested branches require QIR; this QASM2 exporter does not flatten them", "UNSUPPORTED_CAPABILITY")
            value = inst.get("condition_value")
            if qs or params or len(cs) != 1 or type(value) is not int or value not in {0, 1}:
                raise QStackError("QASM2 branch requires one classical bit and a binary condition", "ARTIFACT_INVALID")
            branch = {"bit": cs[0], "value": value, "else": False}
            continue
        if op in {"else", "endif"}:
            if qs or cs or params or branch is None or (op == "else" and branch["else"]):
                raise QStackError("Unbalanced QASM2 branch markers", "ARTIFACT_INVALID")
            if op == "endif":
                branch = None
            else:
                branch["value"] = 1 - branch["value"]
                branch["else"] = True
            continue
        guard = f"if(c[{branch['bit']}]=={branch['value']}) " if branch is not None else ""
        if op == "gphase":
            if qs or cs or len(params) != 1:
                raise QStackError("Invalid QASM2 phase instruction", "ARTIFACT_INVALID")
            lines.append(f"// {guard}Scalar phase retained in physical IR: {numbers[0]} radians.")
            continue
        if op == "measure" and len(qs) == len(cs) == 1 and not params:
            if branch is not None and cs[0] == branch["bit"]:
                raise QStackError("QASM2 branch writes its condition bit; use QIR to preserve the branch-entry predicate", "UNSUPPORTED_CAPABILITY")
            statement = f"measure q[{qs[0]}] -> c[{cs[0]}];"
        elif op == "reset" and len(qs) == 1 and not params and not cs:
            statement = f"reset q[{qs[0]}];"
        elif op == "barrier" and not params and not cs:
            if branch is not None:
                raise QStackError("Conditional barriers require QIR; QASM2 cannot represent them", "UNSUPPORTED_CAPABILITY")
            if not nq:
                continue
            statement = "barrier " + (",".join(f"q[{q}]" for q in qs) if qs else "q") + ";"
        elif not cs and (op, len(qs), len(params)) in {("u1q", 1, 2), ("rz", 1, 1), ("rzz", 2, 1)}:
            name = {"u1q": "U1q", "rz": "Rz", "rzz": "RZZ"}[op]
            statement = f"{name}({','.join(numbers)}) " + ",".join(f"q[{q}]" for q in qs) + ";"
        else:
            raise QStackError(f"Instruction '{op}' is not a native HQSLIB QASM2 operation", "UNSUPPORTED_GATE")
        lines.append(guard + statement)
    if branch is not None:
        raise QStackError("Unclosed QASM2 branch", "ARTIFACT_INVALID")
    return "\n".join(lines) + "\n"
