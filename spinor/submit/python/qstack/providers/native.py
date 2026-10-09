"""Lossless SDK object construction from already lowered physical instructions.

These functions do not optimize, route, decompose, or transpile a circuit.
"""
from __future__ import annotations

import json
from math import isclose, isfinite, pi
from qstack.models import QStackError
from .base import optional, plain
from .calibration import Calibrations, finite_number


def instructions(ir, *, allow_control=False):
    for item in ir.get("instructions", []):
        if any(key in item for key in ("condition", "then", "else", "body")) or (not allow_control and item.get("op") in {"if", "else", "endif", "branch", "while"}):
            raise QStackError("This provider serializer does not support structured control flow", "UNSUPPORTED_CAPABILITY")
        if any(not isinstance(p, (int, float)) or not isfinite(p) for p in item.get("params", [])):
            raise QStackError("Physical gate parameters must be finite numbers")
        yield item


def serialize_native(route, ir, snapshot=None):
    """Serialize native gates; parameter-unit changes are not circuit synthesis.

Global circuit phase has no observable effect on sampling and is retained in IR.
No extra quantum operation is introduced by these wire encodings.
"""
    snapshot = snapshot or {}
    if route == "ionq":
        circuit, measured = [], False
        for item in instructions(ir):
            op, q, p = item["op"].lower(), item.get("qubits", []), item.get("params", [])
            if op == "barrier":
                continue
            if op == "measure":
                measured = True
                continue  # IonQ measures all qubits at the end; mapping remains in receipt.
            if measured:
                raise QStackError("IonQ native JSON only supports terminal measurements")
            if op in {"gpi", "gpi2"} and len(q) == len(p) == 1:
                circuit.append({"gate": op, "target": q[0], "phase": p[0] / (2 * pi)})
            elif op in {"rzz", "zz"} and len(q) == 2 and len(p) == 1:
                circuit.append({"gate": "zz", "targets": q, "angle": p[0] / (2 * pi)})
            elif op == "ms" and len(q) == 2 and len(p) in {0, 2, 3}:
                # Spinor's parameter-free MS is exactly RXX(pi/2).
                gate = {"gate": "ms", "targets": q, "phases": [p[0] / (2 * pi), p[1] / (2 * pi)] if p else [0.0, 0.0],
                        "angle": p[2] / (2 * pi) if len(p) == 3 else 0.25}
                if not 0 <= gate["angle"] <= 0.25:
                    raise QStackError("IonQ MS angle must be compiler-lowered to [0,pi/2] radians", "UNSUPPORTED_GATE")
                circuit.append(gate)
            else:
                raise QStackError(f"IonQ cannot encode native gate '{op}'", "UNSUPPORTED_GATE")
        return "ionq-native-json", json.dumps({"qubits": ir["num_qubits"], "gateset": "native", "circuit": circuit}, allow_nan=False)
    if route == "iqm":
        labels = snapshot.get("qubit_labels") or snapshot.get("raw", {}).get("qubits")
        if not labels or len(labels) < ir["num_qubits"]:
            raise QStackError("IQM native serialization requires physical qubit_labels from the target snapshot", "MISSING_TARGET_SNAPSHOT")
        operations = []
        for item in instructions(ir):
            op, q, p = item["op"].lower(), item.get("qubits", []), item.get("params", [])
            if op == "barrier":
                continue
            args = {}
            if op in {"u1q", "prx"} and len(p) == 2 and len(q) == 1:
                op, args = "prx", {"angle": p[0], "phase": p[1]}
            elif op in {"cz", "move"} and len(q) == 2 and not p:
                pass
            elif op == "measure" and len(q) == len(item.get("clbits", [])) == 1:
                args = {"key": f"c{item['clbits'][0]}"}
            elif op == "reset" and len(q) == 1 and not p:
                pass
            else:
                raise QStackError(f"IQM cannot encode native gate '{op}'", "UNSUPPORTED_GATE")
            operations.append({"name": op, "locus": [labels[index] for index in q], "args": args})
        return "iqm-json", json.dumps({"name": ir.get("name", "qstack"), "instructions": operations}, allow_nan=False)
    if route == "anyon":
        operations, measured = [], False
        fixed = {"id": "i", "x": "x", "y": "y", "z": "z", "sx": "x_90", "sxdg": "x_minus_90",
                 "s": "z_90", "sdg": "z_minus_90", "t": "t", "tdg": "t_dag", "cz": "cz"}
        for item in instructions(ir):
            op, q, p = item["op"].lower(), item.get("qubits", []), item.get("params", [])
            if op == "barrier":
                continue
            if op == "measure":
                measured = True
                if len(q) != 1 or len(item.get("clbits", [])) != 1:
                    raise QStackError("Anyon readout needs one qubit and one destination bit")
                operations.append({"type": "readout", "qubits": q, "bits": item["clbits"]})
                continue
            if measured:
                raise QStackError("Anyon native contract only supports terminal readouts")
            params = {}
            if op in fixed and not p:
                name = fixed[op]
            elif op in {"rz", "p"} and len(p) == len(q) == 1:
                name, params = "p", {"lambda": p[0]}
            elif op in {"rx", "ry"} and len(p) == len(q) == 1 and isclose(abs(p[0]), pi / 2, abs_tol=1e-12):
                name = ("x" if op == "rx" else "y") + ("_90" if p[0] > 0 else "_minus_90")
            else:
                raise QStackError(f"Anyon cannot encode native gate '{op}'", "UNSUPPORTED_GATE")
            operations.append({"type": name, "qubits": q, "parameters": params})
        return "anyon-json", json.dumps({"operations": operations, "qubitCount": ir["num_qubits"], "bitCount": ir["num_clbits"]}, allow_nan=False)
    raise QStackError(f"No direct native JSON serializer for route '{route}'", "UNSUPPORTED_FORMAT")


def qiskit_circuit(artifact, *, allowed=None, module=None):
    sdk = module or optional("qiskit", "ibm")
    ir = artifact.physical_ir
    qc = sdk.QuantumCircuit(ir["num_qubits"], ir["num_clbits"], name=ir.get("name", "qstack"))
    qc.global_phase = ir.get("global_phase", 0)
    fixed = {"id": (1, 0), "x": (1, 0), "y": (1, 0), "z": (1, 0), "sx": (1, 0),
             "sxdg": (1, 0), "h": (1, 0), "s": (1, 0), "sdg": (1, 0), "t": (1, 0), "tdg": (1, 0),
             "cx": (2, 0), "cz": (2, 0), "ecr": (2, 0), "iswap": (2, 0), "swap": (2, 0),
             "rx": (1, 1), "ry": (1, 1), "rz": (1, 1), "r": (1, 2),
             "rxx": (2, 1), "ryy": (2, 1), "rzz": (2, 1), "reset": (1, 0)}
    items = list(instructions(ir, allow_control=True))

    def emit(item):
        op = {"cnot": "cx", "u1q": "r"}.get(item["op"].lower(), item["op"].lower())
        q, p, c = item.get("qubits", []), item.get("params", []), item.get("clbits", [])
        if op == "gphase" and not q and len(p) == 1:
            # Qiskit's public property updates the current control-flow scope.
            qc.global_phase += p[0]
            return
        if allowed is not None and op not in allowed:
            raise QStackError(f"{artifact.route} cannot serialize physical gate '{op}'", "UNSUPPORTED_GATE")
        if op == "measure" and len(q) == len(c) == 1:
            qc.measure(q[0], c[0])
        elif op == "barrier":
            qc.barrier(*q)
        elif op == "delay" and len(q) == len(p) == 1:
            if not item.get("unit"):
                raise QStackError("Delay requires an explicit duration unit")
            qc.delay(p[0], q[0], unit=item["unit"])
        elif op in fixed and (len(q), len(p)) == fixed[op]:
            getattr(qc, op)(*p, *q)
        else:
            raise QStackError(f"Cannot serialize physical instruction '{op}'", "UNSUPPORTED_GATE")

    def block(start, nested=False):
        index = start
        while index < len(items):
            item = items[index]
            op = item["op"].lower()
            if op in {"else", "endif"}:
                if not nested:
                    raise QStackError("Unmatched physical control-flow marker")
                return index
            if op == "if":
                bits, value = item.get("clbits", []), item.get("condition_value")
                if len(bits) != 1 or value not in (0, 1) or not 0 <= bits[0] < ir["num_clbits"]:
                    raise QStackError("IBM branch requires one classical bit and condition_value 0 or 1")
                with qc.if_test((qc.clbits[bits[0]], value)) as otherwise:
                    index = block(index + 1, True)
                if index < len(items) and items[index]["op"].lower() == "else":
                    with otherwise:
                        index = block(index + 1, True)
                if index >= len(items) or items[index]["op"].lower() != "endif":
                    raise QStackError("Physical branch is missing endif")
            else:
                emit(item)
            index += 1
        return index

    block(0)
    return qc


def validate_qiskit_target(ir, target):
    """Check exact ordered ISA loci and parameter bounds without transpilation."""
    if target is None or not callable(getattr(target, "instruction_supported", None)):
        raise QStackError("IBM backend did not provide an ISA Target", "MISSING_TARGET_SNAPSHOT")
    for item in instructions(ir, allow_control=True):
        op = item["op"].lower()
        if op in {"barrier", "gphase", "else", "endif"}:
            continue
        name = {"u1q": "r", "cnot": "cx", "if": "if_else"}.get(op, op)
        kwargs = {"operation_name": name}
        if op != "if":
            kwargs.update(qargs=tuple(item.get("qubits", [])), parameters=item.get("params", []))
        if not target.instruction_supported(**kwargs):
            raise QStackError(f"IBM ISA does not accept '{op}' on the compiled ordered qubits and parameters; refresh and recompile",
                              "TARGET_INCOMPATIBLE")


def cirq_circuit(artifact, config, *, module=None):
    cirq = module or optional("cirq", "google")
    ir = artifact.physical_ir
    stored_labels = artifact.target_snapshot.get("qubit_labels")
    configured_labels = config.get("qubit_labels")
    if stored_labels is not None and configured_labels is not None and configured_labels != stored_labels:
        raise QStackError("Configured qubit_labels differ from the compiled physical mapping; recompile for that mapping", "TARGET_INCOMPATIBLE")
    labels = stored_labels if stored_labels is not None else configured_labels
    if not labels or len(labels) < ir["num_qubits"]:
        raise QStackError("Google requires physical qubit_labels from its device specification", "MISSING_TARGET_SNAPSHOT")
    qubits = []
    for label in labels:
        if isinstance(label, (list, tuple)) and len(label) == 2:
            qubits.append(cirq.GridQubit(int(label[0]), int(label[1])))
        elif isinstance(label, str) and "_" in label:
            row, col = label.split("_", 1)
            qubits.append(cirq.GridQubit(int(row), int(col)))
        else:
            raise QStackError("Google qubit_labels must contain [row,column] or 'row_column'")
    circuit = cirq.Circuit()
    operations = []
    for index, item in enumerate(instructions(ir)):
        op = item["op"].lower()
        qs = [qubits[q] for q in item.get("qubits", [])]
        p = item.get("params", [])
        if op == "measure":
            if len(qs) != 1 or len(item.get("clbits", [])) != 1:
                raise QStackError("Each physical measurement must map one qubit to one classical bit")
            gate = cirq.measure(qs[0], key=f"c{item['clbits'][0]}")
        elif op == "rz" and len(p) == len(qs) == 1:
            gate = cirq.rz(p[0])(qs[0])
        elif op == "u1q" and len(p) == 2 and len(qs) == 1:
            gate = cirq.PhasedXPowGate(exponent=p[0] / pi, phase_exponent=p[1] / pi, global_shift=-0.5)(*qs)
        elif op == "phased_xz" and len(p) == 3 and len(qs) == 1:
            gate = cirq.PhasedXZGate(x_exponent=p[0] / pi, z_exponent=p[1] / pi, axis_phase_exponent=p[2] / pi)(*qs)
        elif op == "cz" and not p and len(qs) == 2:
            gate = cirq.CZ(*qs)
        elif op == "iswap" and not p and len(qs) == 2:
            gate = cirq.ISWAP(*qs)
        elif op in {"sqrt_iswap", "sqrt_iswap_inv"} and not p and len(qs) == 2:
            gate = (cirq.ISWAP ** (0.5 if op == "sqrt_iswap" else -0.5))(*qs)
        elif op == "syc" and not p and len(qs) == 2:
            gate = optional("cirq_google", "google").SYC(*qs)
        elif op == "reset" and len(qs) == 1:
            gate = cirq.reset(qs[0])
        elif op == "barrier":
            continue
        else:
            raise QStackError(f"Google cannot serialize physical instruction '{op}'", "UNSUPPORTED_GATE")
        operations.append((index, item, gate))
    schedule = artifact.manifest.get("statistics", {}).get("schedule")
    if schedule is None:
        for _, _, gate in operations:
            circuit.append(gate, strategy=cirq.InsertStrategy.NEW)
        return circuit
    # Reconstruct the owned compiler's dependency layers, without asking Cirq
    # to optimize or schedule operations. Timings remain the service's concern.
    layers = {row["instruction"]: row["layer"] for row in schedule}
    moments, previous = {}, {}
    for index, item, gate in operations:
        layer = layers.get(index)
        if not isinstance(layer, int) or isinstance(layer, bool) or layer < 0:
            raise QStackError("Compiler schedule is missing a valid instruction layer", "INVALID_ARTIFACT")
        resources = [*(f"q{q}" for q in item.get("qubits", [])), *(f"c{c}" for c in item.get("clbits", []))]
        if any(previous.get(resource, -1) >= layer for resource in resources):
            raise QStackError("Compiler schedule violates an instruction dependency", "INVALID_ARTIFACT")
        for resource in resources:
            previous[resource] = layer
        moments.setdefault(layer, []).append(gate)
    for layer in sorted(moments):
        circuit.append(cirq.Moment(moments[layer]))
    return circuit


def qiskit_target_record(route, backend, formats):
    target = backend.target
    native = list(target.operation_names)
    aliases = {"r": "u1q", "cx": "cx"}
    coupling, gate_loci = [], {}
    calibrations = Calibrations()
    for name in native:
        # None means globally available, rather than an empty allowed-locus set.
        if None not in target[name]:
            gate_loci[aliases.get(name, name)] = [list(locus) for locus in target[name]]
        for locus in target[name]:
            if locus is not None and len(locus) == 2:
                coupling.append(list(locus))
            properties = target[name][locus]
            if locus is not None and properties is not None:
                op = aliases.get(name, name)
                calibrations.add_error(op, locus, getattr(properties, "error", None))
                duration = getattr(properties, "duration", None)
                if finite_number(duration):
                    calibrations.add_duration(op, locus, duration * 1e9)
    # A None locus means an instruction applies to any compatible tuple.
    all_to_all = any(None in target[name] for name in native if getattr(target.operation_from_name(name), "num_qubits", 0) == 2)
    return {"route": route, "device": backend.name, "qubits": target.num_qubits,
            "native_gates": [aliases.get(g, g) for g in native], "coupling": sorted(map(list, set(map(tuple, coupling)))),
            "all_to_all": all_to_all, "directed_connectivity": True,
            "gate_loci": gate_loci,
            "supports": {"reset": "reset" in native, "mid_circuit_measure": "if_else" in native,
                         "feedforward": "if_else" in native},
            "formats": formats, "parameter_units": "radians", "capability_verified": bool(native),
            **calibrations.fields(),
            "raw": {"operation_names": native, "description": str(target.description or "")}}
