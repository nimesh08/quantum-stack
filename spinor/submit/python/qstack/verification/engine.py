"""Complete operators and instruments with arbitrary logical/reference inputs."""
from __future__ import annotations

import numpy as np
from . import NotChecked
from .matrices import apply, matrix

MAX_WORKING_BYTES = 256 * 1024 * 1024


def _classical(item, values, declarations):
    op, result = item["op"], item["result"]
    names = item.get("inputs", [])
    if op == "c_select":
        answer = values[names[1] if values[names[0]] else names[2]]
    else:
        args = [values[n] for n in names]
        a, b = (args + [0, 0])[:2]
        if op == "c_const": answer = int(item["value"])
        elif op in {"c_copy", "c_cast"}: answer = a
        elif op == "c_not": answer = ~a
        elif op == "c_and": answer = a & b
        elif op == "c_or": answer = a | b
        elif op == "c_xor": answer = a ^ b
        elif op == "c_add": answer = a + b
        elif op == "c_sub": answer = a - b
        elif op == "c_eq": answer = a == b
        elif op == "c_ne": answer = a != b
        elif op == "c_lt": answer = a < b
        elif op == "c_le": answer = a <= b
        elif op == "c_gt": answer = a > b
        elif op == "c_ge": answer = a >= b
        elif op in {"c_shl", "c_shr"}:
            if not 0 <= b < declarations[result]["width"]:
                raise ValueError("Shift must be in range for its fixed-width operand")
            answer = a << b if op == "c_shl" else a >> b
        else: raise NotChecked(f"Unknown classical instruction {op}; not skipped")
    values[result] = int(answer) & ((1 << declarations[result]["width"]) - 1)
    return result


def _layout(ir, key):
    return ir.get(key) or list(range(ir["num_qubits"]))


def execute(ir, *, max_qubits, max_paths):
    initial, final = _layout(ir, "initial_logical_to_physical"), _layout(ir, "logical_to_physical")
    if len(initial) != len(final):
        raise NotChecked("Different quantum input/output arities require an explicit ownership interface")
    active = sorted(set(initial) | set(final) | set(ir.get("_active_qubits", [])) | {q for i in ir["instructions"] for q in i.get("qubits", [])})
    if "quantum_inputs" in ir:
        inputs, pool = ir["quantum_inputs"], ir.get("reserved_pool", [])
        if any(type(q) is not int or not 0 <= q < len(initial) for q in [*inputs, *pool]) or \
                len(set(inputs + pool)) != len(inputs + pool) or set(inputs + pool) != set(range(len(initial))):
            raise ValueError("Quantum input/reserved-pool interface must partition the logical wires")
        # Reserved branch/function-local storage starts at |0>, independently
        # of arbitrary input/reference states, and its final state is traced.
        initial = [initial[q] for q in inputs]
        final = [final[q] for q in inputs]
    if len(initial) > max_qubits or len(active) > max_qubits + 2:
        raise NotChecked(f"Complete operator limit exceeded: {len(initial)} logical, {len(active)} active qubits")
    if len(ir["instructions"]) > 100000:
        raise NotChecked("Independent instruction budget exceeded")
    positions = {q: i for i, q in enumerate(active)}
    width, d = len(active), 1 << len(initial)
    # The public qubit/path bounds are independent. Bound their product too:
    # dense instruments grow as 16**logical_qubits per classical outcome.
    operator_bytes = (1 << width) * d * np.dtype(complex).itemsize
    path_limit = min(max_paths, MAX_WORKING_BYTES // max(1, 4 * operator_bytes))
    if path_limit < 1:
        raise NotChecked("Complete operator working-memory budget exceeded")
    injection = np.zeros((1 << width, d), dtype=complex)
    for basis in range(d):
        injection[sum(((basis >> q) & 1) << positions[p] for q, p in enumerate(initial)), basis] = np.exp(1j * ir.get("global_phase", 0))
    declarations = {v["id"]: v for v in ir.get("classical_values", [])}
    initial_values = {v["id"]: int(v["initial_value"]) for v in declarations.values() if v.get("initialized")}
    bits = [0] * ir["num_clbits"]
    for storage in ir.get("classical_storage", []):
        if storage.get("initialized"):
            for index, bit in enumerate(storage["bits"]):
                bits[bit] = (int(storage["initial_value"]) >> index) & 1
    for name, value in initial_values.items():
        for index, bit in enumerate(declarations[name]["storage"]):
            bits[bit] = (value >> index) & 1
    instructions = ir["instructions"]
    branches, stack = {}, []
    for index, item in enumerate(instructions):
        if item["op"] == "if": stack.append([index, None])
        elif item["op"] == "else":
            if not stack or stack[-1][1] is not None: raise ValueError("Unmatched else")
            stack[-1][1] = index
        elif item["op"] == "endif":
            if not stack: raise ValueError("Unmatched endif")
            start, other = stack.pop()
            branches[start] = other, index
    if stack: raise ValueError("Unclosed branch")

    def block(start, stop, trajectories):
        index = start
        while index < stop:
            item = instructions[index]
            op = item["op"]
            qs = [positions[q] for q in item.get("qubits", [])]
            if op == "if":
                other, end = branches[index]
                changed = []
                for bits, values, operator in trajectories:
                    if "condition_expression" in item:
                        from .parsers import classical_expression
                        condition = classical_expression(item["condition_expression"], values, bits, ir.get("_qasm_widths", {}))
                    else:
                        if item.get("condition"):
                            name = item["condition"]
                            if name not in values: raise ValueError("Undefined immutable branch predicate")
                            condition = sum(bits[b] << j for j, b in enumerate(declarations[name]["storage"]))
                        else:
                            condition = bits[item["clbits"][0]]
                    take = condition == item.get("condition_value", 1)
                    lo, hi = (index + 1, other if other is not None else end) if take else (other + 1 if other is not None else end, end)
                    changed.extend(block(lo, hi, [(bits, values, operator)]))
                    if len(changed) > path_limit:
                        raise NotChecked("Complete instrument trajectory budget exceeded at branch join")
                trajectories, index = changed, end + 1
                continue
            changed = []
            for bits, values, operator in trajectories:
                if op in {"measure", "reset"}:
                    for outcome in (0, 1):
                        projector = np.zeros((2, 2), complex)
                        projector[outcome if op == "measure" else 0, outcome] = 1
                        after = apply(projector, qs, operator, width)
                        # Keep all nonzero outcomes, including tiny interactions.
                        if np.any(after):
                            new_bits, new_values = list(bits), dict(values)
                            if op == "measure":
                                new_bits[item["clbits"][0]] = outcome
                                if item.get("result"): new_values[item["result"]] = outcome
                            changed.append((new_bits, new_values, after))
                elif op.startswith("c_"):
                    # The defined-ID set enforces SSA dominance, but the actual
                    # physical controller reads current storage. Keeping a stale
                    # Python value here would hide an illegal coloring alias.
                    new_values = {name: sum(bits[b] << j for j, b in enumerate(declarations[name]["storage"]))
                                  for name in values}
                    new_bits = list(bits)
                    result = _classical(item, new_values, declarations)
                    for j, bit in enumerate(declarations[result]["storage"]):
                        new_bits[bit] = (new_values[result] >> j) & 1
                    changed.append((new_bits, new_values, operator))
                elif op in {"_set_value", "_store_bit"}:
                    from .parsers import classical_expression
                    new_values, new_bits = dict(values), list(bits)
                    value = classical_expression(item["expression"], values, bits, ir.get("_qasm_widths", {}))
                    if op == "_set_value":
                        new_values[item["result"]] = value & ((1 << ir["_qasm_widths"][item["result"]])-1)
                    else: new_bits[item["clbits"][0]] = value & 1
                    changed.append((new_bits, new_values, operator))
                elif op == "barrier": changed.append((bits, values, operator))
                elif op == "gphase": changed.append((bits, values, operator * np.exp(1j * item["params"][0])))
                else:
                    if op == "move":
                        occupied = [b for b in range(1 << width) if all(b & (1 << q) for q in qs)]
                        if np.any(np.abs(operator[occupied, :]) > 1e-13):
                            raise ValueError("MOVE has occupied undefined |11> subspace")
                    changed.append((bits, values, apply(matrix(item), qs, operator, width)))
            trajectories = changed
            if len(trajectories) > path_limit: raise NotChecked("Complete instrument trajectory or working-memory budget exceeded")
            index += 1
        return trajectories
    if "_qir_module" in ir:
        from .qir import run
        trajectories = run(ir, injection, positions, width, path_limit)
    else:
        trajectories = block(0, len(instructions), [(bits, initial_values, injection)])
    outputs = [positions[q] for q in final]
    unused = [q for q in range(width) if q not in outputs]
    reordered, instrument = [], {}
    exported = ir.get("exported_clbits", list(range(ir["num_clbits"])))
    for bits, values, operator in trajectories:
        typed_outputs = tuple((output["name"], output["type"], output["width"],
                               sum(bits[b] << j for j, b in enumerate(declarations[output["value"]]["storage"])))
                              for output in ir.get("classical_outputs", []))
        key = (tuple(bits[b] for b in exported), typed_outputs)
        # Account for both compared instruments plus accumulation temporaries.
        outcomes = len(instrument) + int(key not in instrument)
        if (2 * outcomes + 4) * d**4 * np.dtype(complex).itemsize > MAX_WORKING_BYTES:
            raise NotChecked("Complete instrument working-memory budget exceeded (256 MiB)")
        all_rows = []
        for ancilla in range(1 << len(unused)):
            fixed = sum(((ancilla >> j) & 1) << q for j, q in enumerate(unused))
            rows = [fixed + sum(((basis >> j) & 1) << q for j, q in enumerate(outputs)) for basis in range(d)]
            all_rows.extend(rows)
            vector = operator[rows, :].reshape(-1)
            instrument[key] = instrument.get(key, np.zeros((d*d, d*d), complex)) + np.outer(vector, vector.conj())
        reordered.append(operator[all_rows, :])
    unitary = not ir.get("_nonunitary", False) and not ir.get("reserved_pool") and not any(i["op"] in {"measure", "reset", "if"} for i in instructions)
    return reordered[0] if unitary else None, instrument


def compare(before, after, *, max_qubits, max_paths, threshold, allow_scalar_phase_loss=False):
    u, a = execute(before, max_qubits=max_qubits, max_paths=max_paths)
    v, b = execute(after, max_qubits=max_qubits, max_paths=max_paths)
    instrument_residual = max((float(np.max(np.abs(a.get(key, 0)-b.get(key, 0)), initial=0)) for key in a.keys() | b.keys()), default=0)
    if u is not None and v is not None and not allow_scalar_phase_loss:
        rows = max(u.shape[0], v.shape[0])
        if u.shape[1] != v.shape[1]: raise ValueError("Different quantum input widths")
        u = np.pad(u, ((0, rows-u.shape[0]), (0, 0)))
        v = np.pad(v, ((0, rows-v.shape[0]), (0, 0)))
        residual = max(float(np.max(np.abs(u-v), initial=0)), instrument_residual)
        kind = "complete-phase operator"
    else:
        residual = instrument_residual
        kind = "complete quantum instrument (all classical outcomes, reset environments traced)"
    return {"status": "passed" if residual <= threshold else "failed", "method": kind,
            "max_entry_residual": residual, "threshold": threshold, "evaluation_precision": "complex128",
            "reason": "Finite numerical comparison; not a certified error bound"}
