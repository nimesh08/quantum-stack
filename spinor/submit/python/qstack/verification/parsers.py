"""Strict independent interpreters for emitted formats. Unknown text is an error.

Only syntax and semantics in this module are accepted; no compiler parser,
serializer or simulator is used to obtain the expected operator.
"""
from __future__ import annotations

import ast
import copy
import json
import math
import re

from . import NotChecked


def number(text, bindings=None):
    names = {"pi": math.pi, **(bindings or {})}
    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) in {int, float}: return node.value
        if isinstance(node, ast.Name) and node.id in names: return names[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            return visit(node.operand) * (-1 if isinstance(node.op, ast.USub) else 1)
        if isinstance(node, ast.BinOp):
            a, b = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): return a+b
            if isinstance(node.op, ast.Sub): return a-b
            if isinstance(node.op, ast.Mult): return a*b
            if isinstance(node.op, ast.Div): return a/b
        raise NotChecked(f"Unsupported numerical expression: {text}")
    value = visit(ast.parse(text.strip(), mode="eval").body)
    if not math.isfinite(value): raise ValueError("Nonfinite gate parameter")
    return value


def _base(artifact):
    result = copy.deepcopy(artifact.physical_ir)
    result["instructions"], result["global_phase"] = [], 0
    # Retain the public mapping, never the expected program's initialization.
    # The emitted language must perform those stores itself; importing them
    # here would conceal a serializer that dropped an initialization.
    for value in [*result.get("classical_values", []), *result.get("classical_storage", [])]:
        value["initialized"] = False
    return result


def classical_expression(text, values, bits, widths=None):
    """Evaluate only the emitted finite-width expression grammar, without eval."""
    widths = widths or {}
    def width_of(node):
        if isinstance(node, ast.Name): return widths.get(node.id)
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name) and node.value.id == "c": return 1
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Subscript) and isinstance(node.func.value, ast.Name) and node.func.value.id == "uint": return visit(node.func.slice)
        if isinstance(node, ast.BinOp): return width_of(node.left)
        return None
    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) in {int, bool}: return int(node.value)
        if isinstance(node, ast.Name) and node.id in values: return values[node.id]
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name) and node.value.id == "c": return bits[visit(node.slice)]
        if isinstance(node, ast.UnaryOp):
            value = visit(node.operand)
            if isinstance(node.op, ast.Invert): return ~value
            if isinstance(node.op, ast.Not): return not value
            if isinstance(node.op, ast.USub): return -value
        if isinstance(node, ast.Call) and len(node.args) == 1 and not node.keywords:
            if isinstance(node.func, ast.Name) and node.func.id == "bit": return visit(node.args[0]) & 1
            if isinstance(node.func, ast.Subscript) and isinstance(node.func.value, ast.Name) and node.func.value.id == "uint":
                width = visit(node.func.slice)
                if not 1 <= width <= 64: raise ValueError("Unsupported classical cast width")
                return visit(node.args[0]) & ((1 << width)-1)
        if isinstance(node, ast.BinOp):
            a, b = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): return a+b
            if isinstance(node.op, ast.Sub): return a-b
            if isinstance(node.op, ast.BitAnd): return a&b
            if isinstance(node.op, ast.BitOr): return a|b
            if isinstance(node.op, ast.BitXor): return a^b
            if isinstance(node.op, (ast.LShift, ast.RShift)):
                width = width_of(node.left)
                if width is None: raise NotChecked("Shift operand width is unknown")
                if not 0 <= b < width: raise ValueError("Shift must be in range for its fixed-width operand")
                return a << b if isinstance(node.op, ast.LShift) else a >> b
        if isinstance(node, ast.Compare) and len(node.ops) == 1:
            a, b, op = visit(node.left), visit(node.comparators[0]), node.ops[0]
            if isinstance(op, ast.Eq): return a == b
            if isinstance(op, ast.NotEq): return a != b
            if isinstance(op, ast.Lt): return a < b
            if isinstance(op, ast.LtE): return a <= b
            if isinstance(op, ast.Gt): return a > b
            if isinstance(op, ast.GtE): return a >= b
        raise NotChecked(f"Unknown classical expression {text}; not skipped")
    return int(visit(ast.parse(text.strip(), mode="eval").body))


def qasm(text, ir):
    text = re.sub(r"//[^\n]*", "", text)
    definitions = {}
    def define(match):
        name, params, qubits, body = match.groups()
        definitions[name] = ([p.strip() for p in (params or "").split(",") if p.strip()],
                             [q.strip() for q in qubits.split(",")], body)
        return ""
    text = re.sub(r"\bgate\s+(\w+)\s*(?:\(([^)]*)\))?\s+([\w\s,]+)\s*\{([^{}]*)\}", define, text)
    text = re.sub(r"(?m)^\s*#pragma braket verbatim\s*$", "", text)
    # The sole supported box is Braket's verbatim region. It has no control effect.
    text = re.sub(r"\bbox\s*\{([^{}]*)\}", r"\1", text)
    tokens = [t.strip() for t in re.split(r"(;|\{|\})", text) if t.strip() and t != ";"]
    instructions = []
    ir["_qasm_widths"] = {}
    def gate(statement, parameters=None, wires=None, depth=0):
        if depth > 32: raise NotChecked("Custom-gate expansion depth exceeded")
        match = re.fullmatch(r"(\w+)\s*(?:\((.*)\))?\s*(.*?)", statement)
        if not match: raise NotChecked(f"Unknown QASM statement: {statement}")
        name, arguments, operands = match.groups()
        params = [number(p, parameters) for p in arguments.split(",")] if arguments else []
        qs = []
        for operand in operands.split(",") if operands else []:
            operand = operand.strip()
            ref = re.fullmatch(r"q\[(\d+)\]|\$(\d+)", operand)
            if ref: qs.append(int(next(g for g in ref.groups() if g is not None)))
            elif wires is not None and operand in wires: qs.append(wires[operand])
            elif name == "barrier" and operand == "q": qs.extend(range(ir["num_qubits"]))
            else: raise NotChecked(f"Unknown quantum reference '{operand}'")
        if name in definitions:
            formals, formal_wires, body = definitions[name]
            if len(formals) != len(params) or len(formal_wires) != len(qs): raise ValueError("Custom gate arity mismatch")
            return [item for piece in body.split(";") if piece.strip() for item in gate(piece.strip(), dict(zip(formals, params)), dict(zip(formal_wires, qs)), depth+1)]
        normalized = {"U1q": "u1q", "Rz": "rz", "RZZ": "rzz"}.get(name, name.lower())
        return [{"op": normalized, "qubits": qs, "params": params, "clbits": []}]
    index, control = 0, []
    while index < len(tokens):
        t = tokens[index]
        if re.fullmatch(r"OPENQASM [23]\.0|include \"(?:stdgates|hqslib1)\.inc\"", t): pass
        elif match := re.fullmatch(r"(?:qubit\[(\d+)\] q|qreg q\[(\d+)\])", t):
            ir["num_qubits"] = int(next(g for g in match.groups() if g is not None))
        elif match := re.fullmatch(r"(?:bit\[(\d+)\] c|creg c\[(\d+)\])", t):
            ir["num_clbits"] = int(next(g for g in match.groups() if g is not None))
        elif match := re.fullmatch(r"uint\[(\d+)\]\s+(\w+)\s*=\s*(\d+)", t):
            width, name, value = match.groups()
            if not 1 <= int(width) <= 64 or name in ir["_qasm_widths"]: raise ValueError("Invalid classical declaration")
            ir["_qasm_widths"][name] = int(width)
            instructions.append({"op": "_set_value", "result": name, "expression": value})
        elif match := re.fullmatch(r"if\s*\(\s*c\[(\d+)\]\s*==\s*([01])\s*\)\s*(.*)", t):
            bit, value, tail = match.groups()
            instructions.append({"op": "if", "clbits": [int(bit)], "condition_value": int(value)})
            if tail:
                # HQSLIB QASM2 single-instruction conditional, evaluated afresh.
                mini = _decode_statement(tail, gate)
                instructions.extend(mini)
                instructions.append({"op": "endif"})
            else:
                if index+1 >= len(tokens) or tokens[index+1] != "{": raise ValueError("Expected branch body")
                control.append("then")
                index += 1
        elif match := re.fullmatch(r"if\s*\((.*)\)", t):
            if index+1 >= len(tokens) or tokens[index+1] != "{": raise ValueError("Expected branch body")
            instructions.append({"op": "if", "condition_expression": match[1], "condition_value": 1})
            control.append("then")
            index += 1
        elif t == "}":
            if not control: raise ValueError("Unmatched QASM closing brace")
            if index+1 < len(tokens) and tokens[index+1] == "else":
                if control[-1] != "then" or index+2 >= len(tokens) or tokens[index+2] != "{": raise ValueError("Invalid else")
                control[-1] = "else"
                instructions.append({"op": "else"})
                index += 2
            else:
                control.pop()
                instructions.append({"op": "endif"})
        else: instructions.extend(_decode_statement(t, gate))
        index += 1
    if control: raise ValueError("Unclosed QASM branch")
    ir["instructions"] = instructions
    return ir


def _decode_statement(statement, gate):
    match = re.fullmatch(r"c\[(\d+)\]\s*=\s*measure\s+(?:q\[(\d+)\]|\$(\d+))", statement)
    if match:
        c, a, b = match.groups()
        return [{"op": "measure", "qubits": [int(a or b)], "clbits": [int(c)]}]
    match = re.fullmatch(r"measure\s+q\[(\d+)\]\s*->\s*c\[(\d+)\]", statement)
    if match:
        return [{"op": "measure", "qubits": [int(match[1])], "clbits": [int(match[2])]}]
    match = re.fullmatch(r"c\[(\d+)\]\s*=\s*(.*)", statement)
    if match: return [{"op": "_store_bit", "clbits": [int(match[1])], "expression": match[2]}]
    match = re.fullmatch(r"(\w+)\s*=\s*(.*)", statement)
    if match: return [{"op": "_set_value", "result": match[1], "expression": match[2]}]
    return gate(statement)


def native(artifact, ir):
    data = json.loads(artifact.program_text())
    fmt, ops = artifact.format, []
    if fmt in {"json", "cirq-native", "aqt-native", "qibolab-native"}:
        if not isinstance(data, dict) or not isinstance(data.get("instructions"), list): raise ValueError("Native instruction array missing")
        return data
    if fmt == "ionq-native-json":
        ir["num_qubits"] = data["qubits"]
        if data["gateset"] != "native": raise ValueError("IonQ program is not native")
        for item in data["circuit"]:
            op = item["gate"]
            if op in {"gpi", "gpi2"}: p, qs = [item["phase"]*2*math.pi], [item["target"]]
            elif op == "zz": op, p, qs = "rzz", [item["angle"]*2*math.pi], item["targets"]
            elif op == "ms": p, qs = [*(a*2*math.pi for a in item["phases"]), item["angle"]*2*math.pi], item["targets"]
            else: raise NotChecked(f"Unknown IonQ native gate {op}")
            ops.append({"op": op, "qubits": qs, "params": p})
        # IonQ implicitly reads all qubits; output selection is the receipt map.
        ops.extend({"op": "measure", "qubits": [m["qubit"]], "clbits": [m["clbit"]]} for m in artifact.physical_ir.get("measurement_mapping", []))
    elif fmt == "iqm-json":
        labels = artifact.target_snapshot.get("qubit_labels", [])
        for item in data["instructions"]:
            op, args = item["name"], item["args"]
            decoded = {"op": op, "qubits": [labels.index(q) for q in item["locus"]], "params": []}
            if op == "prx": decoded["params"] = [args["angle"], args["phase"]]
            elif op == "measure":
                match = re.fullmatch(r"c(\d+)(?:__\d+)?", args["key"])
                if not match: raise NotChecked("IQM readout key is outside the documented compiler mapping")
                decoded["clbits"] = [int(match[1])]
            elif op not in {"cz", "move", "reset"}: raise NotChecked(f"Unknown IQM instruction {op}")
            ops.append(decoded)
    elif fmt == "anyon-json":
        ir.update(num_qubits=data["qubitCount"], num_clbits=data["bitCount"])
        names = {"i": "id", "x_90": "sx", "x_minus_90": "sxdg", "z_90": "s", "z_minus_90": "sdg", "t_dag": "tdg"}
        for item in data["operations"]:
            op = item["type"]
            decoded = {"op": names.get(op, op), "qubits": item["qubits"], "params": []}
            if op == "readout": decoded.update(op="measure", clbits=item["bits"])
            elif op == "p": decoded["params"] = [item["parameters"]["lambda"]]
            elif op in {"y_90", "y_minus_90"}: decoded.update(op="ry", params=[math.pi/2 if op == "y_90" else -math.pi/2])
            elif op not in {*names, "x", "y", "z", "t", "cz"}: raise NotChecked(f"Unknown Anyon instruction {op}")
            ops.append(decoded)
    else: raise NotChecked(f"No independent native parser for {fmt}")
    ir["instructions"] = ops
    return ir


def quil(text, ir):
    lines, definitions, ops = text.splitlines(), {}, []
    index, branches = 0, []
    while index < len(lines):
        line = lines[index].strip()
        index += 1
        if not line or line.startswith("#"): continue
        if match := re.fullmatch(r"DEFGATE (\w+):", line):
            rows = []
            while index < len(lines) and lines[index].startswith("    "):
                rows.append([complex(v.strip().strip("()").replace("i", "j")) for v in lines[index].split(",")])
                index += 1
            definitions[match[1]] = rows
        elif match := re.fullmatch(r"DECLARE ro BIT\[(\d+)\]", line): ir["num_clbits"] = int(match[1])
        elif match := re.fullmatch(r"JUMP-(UNLESS|WHEN) @SPINOR_ELSE_(\d+) ro\[(\d+)\]", line):
            branches.append((match[2], False))
            ops.append({"op": "if", "clbits": [int(match[3])], "condition_value": 1 if match[1] == "UNLESS" else 0})
        elif match := re.fullmatch(r"JUMP @SPINOR_END_(\d+)", line):
            if not branches or branches[-1][0] != match[1]: raise ValueError("Unmatched Quil jump")
            branches[-1] = (match[1], True)
        elif match := re.fullmatch(r"LABEL @SPINOR_(ELSE|END)_(\d+)", line):
            if not branches or branches[-1][0] != match[2]: raise ValueError("Unmatched Quil label")
            if match[1] == "ELSE" and branches[-1][1]: ops.append({"op": "else"})
            elif match[1] == "END": branches.pop(); ops.append({"op": "endif"})
        elif match := re.fullmatch(r"MEASURE (\d+) ro\[(\d+)\]", line): ops.append({"op": "measure", "qubits": [int(match[1])], "clbits": [int(match[2])]})
        elif match := re.fullmatch(r"(DAGGER )?(\w+)(?:\((.*)\))?((?: \d+)*)", line):
            dagger, name, params, qubits = match.groups()
            op = {"CNOT": "cx", "FENCE": "barrier"}.get(name, name.lower())
            if dagger:
                if name not in {"S", "T"}: raise NotChecked("Unsupported Quil DAGGER operation")
                op += "dg"
            item = {"op": op, "qubits": [int(q) for q in qubits.split()], "params": [number(p) for p in params.split(",")] if params else []}
            if name in definitions: item["matrix"] = definitions[name]
            ops.append(item)
        else: raise NotChecked(f"Unrecognized Quil statement: {line}")
    if branches: raise ValueError("Unclosed Quil control flow")
    ir["instructions"] = ops
    return ir


def decode(artifact):
    ir, fmt = _base(artifact), artifact.format
    if fmt in {"qasm3", "qasm2"}:
        braket = "#pragma braket verbatim" in artifact.program_text()
        gaps = ["Format omits scalar phase; complete instruments checked instead"] if fmt == "qasm2" or braket else []
        return qasm(artifact.program_text(), ir), gaps
    if fmt == "quil": return quil(artifact.program_text(), ir), ["Quil export omits scalar phase; complete instruments checked instead"]
    if fmt.startswith("qir"):
        from .qir import decode_qir
        return decode_qir(artifact, ir), ["QIR omits scalar phase; complete instruments checked instead"]
    gaps = [] if fmt == "json" else ["Native sampling format; scalar phase is not observable"]
    if fmt in {"cirq-native", "aqt-native", "qibolab-native"}:
        gaps.append("Stored native IR checked; actual transport construction is covered by separate offline SDK contracts")
    return native(artifact, ir), gaps
