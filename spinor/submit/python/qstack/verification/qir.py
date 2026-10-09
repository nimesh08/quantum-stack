"""Independent LLVM/QIR interpreter with quantum instruments, not stub readouts."""
from __future__ import annotations
from . import NotChecked


def decode_qir(artifact, ir):
    import pyqir
    context = pyqir.Context()
    module = (pyqir.Module.from_bitcode(context, artifact.program_bytes()) if artifact.format in {"qir", "qir-bitcode"}
              else pyqir.Module.from_ir(context, artifact.program_text()))
    problem = module.verify()
    if problem: raise ValueError("Invalid LLVM module: " + problem)
    functions = [f for f in module.functions if f.name == artifact.manifest.get("qir_entry_point", "main")]
    if len(functions) != 1 or not functions[0].basic_blocks: raise ValueError("QIR entry point missing")
    active, dynamic = set(), False
    for block in functions[0].basic_blocks:
        for inst in block.instructions:
            if isinstance(inst, pyqir.Call) and inst.callee.name.startswith("__quantum__qis__"):
                name = inst.callee.name
                if "read_result" in name: continue
                pointers = [pyqir.ptr_id(a) for a in inst.args if isinstance(a.type, pyqir.PointerType)]
                if "mz__body" in name: pointers = pointers[:-1]
                active.update(p for p in pointers if p is not None)
                dynamic |= any(g in name for g in ("mz__body", "reset__body"))
    ir.update(_qir_module=module, _qir_entry=functions[0], _active_qubits=list(active), _nonunitary=dynamic)
    return ir


def run(ir, injection, positions, width, max_paths):
    import pyqir
    import numpy as np
    from .matrices import apply, matrix
    entry = ir["_qir_entry"]
    # Work items contain a complete path's operator and SSA environment. No
    # measurement outcome is guessed; both projectors are applied to every path.
    pending = [(entry.basic_blocks[0], None, {}, {}, [], injection, 0)]
    completed = []
    while pending:
        block, previous, values, results, outputs, operator, steps = pending.pop()
        def value(v):
            return v.value if isinstance(v, (pyqir.IntConstant, pyqir.FloatConstant)) else values[v.name]
        while True:
            advance = False
            for offset, inst in enumerate(block.instructions):
                steps += 1
                if steps > 100000: raise NotChecked("QIR execution step budget exceeded")
                if isinstance(inst, pyqir.Phi):
                    incoming = next((v for v, b in inst.incoming if b.name == previous), None)
                    if incoming is None: raise ValueError("QIR phi has no matching predecessor")
                    values[inst.name] = value(incoming)
                elif isinstance(inst, pyqir.Call):
                    name, args = inst.callee.name, inst.args
                    if name == "__quantum__rt__initialize": continue
                    if name == "__quantum__rt__array_record_output":
                        if value(args[0]) != ir["num_clbits"]: raise ValueError("QIR output width differs from artifact")
                        continue
                    if name in {"__quantum__rt__read_result", "__quantum__qis__read_result__body"}:
                        values[inst.name] = results[pyqir.ptr_id(args[0])]
                        continue
                    if name in {"__quantum__rt__result_record_output", "__quantum__rt__bool_record_output"}:
                        outputs.append(results[pyqir.ptr_id(args[0])] if "result_record" in name else value(args[0]))
                        continue
                    if not name.startswith("__quantum__qis__"):
                        raise NotChecked(f"Unknown QIR call {name}; not skipped")
                    operation = name.removeprefix("__quantum__qis__")
                    op = {"s__adj": "sdg", "t__adj": "tdg", "cnot__body": "cx", "rxy__body": "u1q", "mz__body": "measure"}.get(operation, operation.removesuffix("__body"))
                    if op == "measure":
                        q, result = positions[pyqir.ptr_id(args[0])], pyqir.ptr_id(args[1])
                        # Resume the remainder of this block as a lightweight
                        # block view; phi nodes occur only before measurements.
                        class Tail:
                            name = block.name
                            instructions = list(block.instructions)[offset+1:]
                        for outcome in (0, 1):
                            p = np.zeros((2, 2), complex); p[outcome, outcome] = 1
                            after = apply(p, [q], operator, width)
                            if np.any(after):
                                pending.append((Tail(), previous, dict(values), {**results, result: outcome}, list(outputs), after, steps))
                        advance = None
                        break
                    numeric = [value(a) for a in args if isinstance(a, (pyqir.IntConstant, pyqir.FloatConstant))]
                    qs = [positions[pyqir.ptr_id(a)] for a in args if isinstance(a.type, pyqir.PointerType)]
                    if op == "reset":
                        class Tail:
                            name = block.name
                            instructions = list(block.instructions)[offset+1:]
                        for outcome in (0, 1):
                            p = np.zeros((2, 2), complex); p[0, outcome] = 1
                            after = apply(p, qs, operator, width)
                            if np.any(after): pending.append((Tail(), previous, dict(values), dict(results), list(outputs), after, steps))
                        advance = None
                        break
                    operator = apply(matrix({"op": op, "params": numeric, "qubits": qs}), qs, operator, width)
                elif inst.opcode == pyqir.Opcode.BR:
                    previous = block.name
                    operands = inst.operands
                    block = operands[0] if len(operands) == 1 else operands[2 if value(operands[0]) else 1]
                    advance = True
                    break
                elif inst.opcode == pyqir.Opcode.RET:
                    if len(outputs) != ir["num_clbits"]: raise ValueError("QIR dropped or added an output")
                    completed.append((outputs, values, operator))
                    advance = None
                    break
                elif isinstance(inst, pyqir.ICmp):
                    a, b = [value(v) for v in inst.operands]
                    predicate = inst.predicate
                    answers = {pyqir.IntPredicate.EQ: a == b, pyqir.IntPredicate.NE: a != b,
                               pyqir.IntPredicate.ULT: a < b, pyqir.IntPredicate.ULE: a <= b,
                               pyqir.IntPredicate.UGT: a > b, pyqir.IntPredicate.UGE: a >= b}
                    if predicate not in answers: raise NotChecked("Signed LLVM comparisons are outside this verifier's uint contract")
                    values[inst.name] = int(answers[predicate])
                elif inst.opcode in {pyqir.Opcode.ADD, pyqir.Opcode.SUB, pyqir.Opcode.AND, pyqir.Opcode.OR, pyqir.Opcode.XOR,
                                      pyqir.Opcode.SHL, pyqir.Opcode.LSHR, pyqir.Opcode.ZEXT, pyqir.Opcode.TRUNC, pyqir.Opcode.SELECT}:
                    operands = inst.operands
                    if inst.opcode == pyqir.Opcode.SELECT:
                        answer = value(operands[1] if value(operands[0]) else operands[2])
                    else:
                        args = [value(v) for v in operands]
                        a, b = (args + [0])[:2]
                        if inst.opcode == pyqir.Opcode.ADD: answer = a+b
                        elif inst.opcode == pyqir.Opcode.SUB: answer = a-b
                        elif inst.opcode == pyqir.Opcode.AND: answer = a&b
                        elif inst.opcode == pyqir.Opcode.OR: answer = a|b
                        elif inst.opcode == pyqir.Opcode.XOR: answer = a^b
                        elif inst.opcode in {pyqir.Opcode.ZEXT, pyqir.Opcode.TRUNC}: answer = a
                        else:
                            if not 0 <= b < inst.type.width: raise ValueError("LLVM shift is poison (out of range)")
                            answer = a << b if inst.opcode == pyqir.Opcode.SHL else a >> b
                    values[inst.name] = answer & ((1 << inst.type.width)-1)
                else:
                    raise NotChecked(f"Unknown LLVM instruction {inst.opcode}; not skipped")
            if len(pending) + len(completed) > max_paths: raise NotChecked("QIR complete-instrument trajectory budget exceeded")
            if not advance: break
    return completed
