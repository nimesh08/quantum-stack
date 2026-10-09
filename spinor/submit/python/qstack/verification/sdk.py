"""Independent interpretation of actual SDK objects, without SDK simulation.

Gate-local public SDK matrices define the transported instruction semantics.
The complete operator/instrument is assembled by our independent oracle. No
compiler matrix, serializer, transpiler or simulator supplies the expectation.
Unsupported nonunitary operations fail closed instead of being skipped.
"""
from __future__ import annotations

import copy
import math
import re

from . import NotChecked


def _base(template):
    ir = copy.deepcopy(template)
    ir["instructions"] = []
    ir["global_phase"] = 0
    for value in [*ir.get("classical_values", []), *ir.get("classical_storage", [])]:
        value["initialized"] = False
    return ir


def qiskit_object(circuit, template):
    """Read QuantumCircuit scopes, actual operands, phase and readout writes."""
    from qiskit.circuit import Clbit
    ir = _base(template)
    if circuit.num_qubits != ir["num_qubits"] or circuit.num_clbits != ir["num_clbits"]:
        raise ValueError("SDK circuit register sizes differ from the compiled interface")
    if circuit.num_clbits and (len(circuit.cregs) != 1 or circuit.cregs[0].name != "c" or len(circuit.cregs[0]) != circuit.num_clbits):
        raise ValueError("IBM result register must match the adapter's single 'c' readout contract")

    def block(scope, qs, cs):
        out = []
        phase = float(scope.global_phase)
        if phase:
            out.append({"op": "gphase", "params": [phase]})
        for entry in scope.data:
            operation = entry.operation
            qubits = [qs[scope.find_bit(q).index] for q in entry.qubits]
            clbits = [cs[scope.find_bit(c).index] for c in entry.clbits]
            name = operation.name
            if name == "if_else":
                condition = operation.condition
                if not isinstance(condition, tuple) or len(condition) != 2:
                    raise NotChecked("SDK classical expression conditions need a separate interpreter")
                bit, value = condition
                if not isinstance(bit, Clbit):
                    raise NotChecked("Only one-bit SDK Clbit conditions are covered; register conditions are not skipped")
                if bit not in scope.clbits:
                    raise ValueError("SDK branch condition refers to an undeclared classical bit")
                try:
                    condition_bit = cs[scope.find_bit(bit).index]
                except (TypeError, AttributeError) as exc:
                    raise NotChecked("Only one-bit SDK conditions are covered") from exc
                if value not in (0, 1):
                    raise ValueError("One-bit SDK condition has an invalid comparison value")
                out.append({"op": "if", "clbits": [condition_bit], "condition_value": int(value)})
                arms = operation.blocks
                if len(arms) not in (1, 2):
                    raise NotChecked("Unexpected SDK branch arity")
                out.extend(block(arms[0], qubits, clbits))
                if len(arms) == 2:
                    out.append({"op": "else"})
                    out.extend(block(arms[1], qubits, clbits))
                out.append({"op": "endif"})
            elif name in {"measure", "reset"}:
                if len(qubits) != 1 or len(clbits) != (name == "measure"):
                    raise ValueError("Malformed SDK measurement/reset")
                out.append({"op": name, "qubits": qubits, "clbits": clbits})
            elif name == "barrier":
                out.append({"op": "barrier", "qubits": qubits})
            elif name == "delay":
                raise NotChecked("SDK delay/noise semantics are outside ideal-operator verification")
            else:
                if clbits or getattr(operation, "condition", None) is not None:
                    raise NotChecked(f"Unsupported SDK classical instruction {name}")
                try:
                    local = operation.to_matrix()
                except (AttributeError, TypeError, ValueError, NotImplementedError) as exc:
                    raise NotChecked(f"No independent SDK matrix for {name}; instruction not skipped") from exc
                # Qiskit local matrices number the first operand least-significantly.
                out.append({"op": "sdk_unitary", "qubits": list(reversed(qubits)), "matrix": local})
        return out

    ir["instructions"] = block(circuit, list(range(circuit.num_qubits)), list(range(circuit.num_clbits)))
    return ir


def _readout_contract(mapping, width):
    if mapping is None:
        return None
    result = {}
    for item in mapping:
        key, bit = item["key"], item["clbit"]
        if not isinstance(key, str) or key in result or type(bit) is not int or not 0 <= bit < width:
            raise ValueError("Invalid or duplicate SDK receipt readout mapping")
        result[key] = bit
    return result


def cirq_object(circuit, template, qubit_labels, measurement_keys=None):
    """Read native Cirq operations, including repeated readout-key writes."""
    import cirq
    ir = _base(template)
    labels = [tuple(map(int, label.split("_"))) if isinstance(label, str) else tuple(label) for label in qubit_labels]
    mapping = {cirq.GridQubit(*label): index for index, label in enumerate(labels)}
    readout = _readout_contract(measurement_keys, ir["num_clbits"])
    seen = set()
    columns = {}
    for operation in circuit.all_operations():
        try:
            qubits = [mapping[q] for q in operation.qubits]
        except KeyError as exc:
            raise ValueError("SDK instruction uses a qubit absent from its target snapshot") from exc
        if cirq.is_measurement(operation):
            gate = getattr(operation, "gate", None)
            if not isinstance(gate, cirq.MeasurementGate):
                raise NotChecked("Only explicit SDK MeasurementGate readouts are covered; wrapped measurements are not skipped")
            if gate.confusion_map:
                raise NotChecked("SDK measurement confusion_map is outside ideal-instrument verification; readout noise is not skipped")
            key = cirq.measurement_key_name(operation)
            match = re.fullmatch(r"c(\d+)(?:__\d+)?", key)
            if not match or len(qubits) != 1 or any(gate.invert_mask):
                raise NotChecked("Only the documented one-bit SDK readout contract is covered")
            if readout is not None and (key not in readout or key in seen):
                raise ValueError("Cirq measurement key differs from the actual receipt retrieval contract")
            seen.add(key)
            bit = readout[key] if readout is not None else int(match[1])
            if not 0 <= bit < ir["num_clbits"]:
                raise ValueError("SDK measurement output exceeds the declared register")
            if readout is not None:
                # Engine returns independent per-key records. Classical final
                # writes follow receipt order, even if disjoint measurements
                # appear in a different moment/operation iteration order.
                columns[key] = ir["num_clbits"]
                ir["num_clbits"] += 1
                bit = columns[key]
            ir["instructions"].append({"op": "measure", "qubits": qubits, "clbits": [bit]})
        elif isinstance(getattr(operation, "gate", None), cirq.ResetChannel) and len(qubits) == 1:
            ir["instructions"].append({"op": "reset", "qubits": qubits})
        else:
            local = cirq.unitary(operation, default=None)
            if local is None:
                raise NotChecked(f"No independent SDK operator for {operation}; instruction not skipped")
            # Cirq and this oracle number the first local operand most-significantly.
            ir["instructions"].append({"op": "sdk_unitary", "qubits": qubits, "matrix": local})
    if readout is not None:
        if seen != set(readout):
            raise ValueError("Cirq circuit dropped a readout required by the receipt")
        ir["exported_clbits"] = template.get("exported_clbits", list(range(template["num_clbits"])))
        for key, bit in readout.items():
            ir["instructions"].append({"op": "_store_bit", "clbits": [bit], "expression": f"c[{columns[key]}]"})
    return ir


def iqm_object(circuit, template, qubit_labels, measurement_keys=None):
    """Interpret the actual IQM CircuitOperation values, not their input JSON."""
    ir = _base(template)
    readout = _readout_contract(measurement_keys, ir["num_clbits"])
    seen = set()
    for operation in circuit.instructions:
        name, args = operation.name, operation.args
        qs = [qubit_labels.index(label) for label in operation.locus]
        item = {"op": name, "qubits": qs, "params": []}
        if name == "prx" and len(qs) == 1 and set(args) == {"angle", "phase"}:
            item.update(op="u1q", params=[args["angle"], args["phase"]])
        elif name == "measure" and len(qs) == 1 and set(args) == {"key"}:
            if readout is not None:
                if args["key"] not in readout or args["key"] in seen:
                    raise ValueError("IQM measurement key differs from its receipt contract")
                seen.add(args["key"])
                item["clbits"] = [readout[args["key"]]]
            else:
                match = re.fullmatch(r"c(\d+)(?:__\d+)?", args["key"])
                if not match:
                    raise NotChecked("Unknown IQM readout key; instruction not skipped")
                item["clbits"] = [int(match[1])]
        elif name in {"cz", "move"} and len(qs) == 2 and not args:
            pass
        elif name == "reset" and len(qs) == 1 and not args:
            pass
        else:
            raise NotChecked(f"Unknown IQM instruction/argument contract {name}; instruction not skipped")
        ir["instructions"].append(item)
    if readout is not None and seen != set(readout):
        raise ValueError("IQM receipt includes readout keys absent from the actual circuit")
    return ir


def aqt_body(body, template):
    """Interpret Arnica's submitted R/RZ/RXX half-turns and terminal MEASURE."""
    ir = _base(template)
    if body["job_type"] != "quantum_circuit" or len(body["payload"]["circuits"]) != 1:
        raise ValueError("Unexpected AQT submission envelope")
    circuit = body["payload"]["circuits"][0]
    ir["num_qubits"] = circuit["number_of_qubits"]
    measured = False
    for operation in circuit["quantum_circuit"]:
        name = operation["operation"]
        if measured:
            raise ValueError("AQT operations occur after its all-qubit measurement")
        if name == "R" and set(operation) == {"operation", "qubit", "theta", "phi"}:
            item = {"op": "u1q", "qubits": [operation["qubit"]], "params": [math.pi*operation["theta"], math.pi*operation["phi"]]}
        elif name == "RZ" and set(operation) == {"operation", "qubit", "phi"}:
            item = {"op": "rz", "qubits": [operation["qubit"]], "params": [math.pi*operation["phi"]]}
        elif name == "RXX" and set(operation) == {"operation", "qubits", "theta"}:
            item = {"op": "rxx", "qubits": operation["qubits"], "params": [math.pi*operation["theta"]]}
        elif name == "MEASURE" and set(operation) == {"operation"}:
            measured = True
            mapping = template.get("measurement_mapping", [])
            # All-qubit hardware readout also dephases unexported quantum wires.
            # The oracle does not silently invent a retained-quantum contract.
            if {m["qubit"] for m in mapping} != set(range(ir["num_qubits"])):
                raise NotChecked("AQT implicit all-qubit readout with partially retained quantum outputs is outside this instrument oracle")
            ir["instructions"].extend({"op": "measure", "qubits": [m["qubit"]], "clbits": [m["clbit"]]} for m in mapping)
            continue
        else:
            raise NotChecked(f"Unknown AQT operation {name}; instruction not skipped")
        ir["instructions"].append(item)
    return ir


def qibolab_plan(plan, template, qubit_ids):
    """Read actual assembler commands; pulse-template calibration is separate."""
    ir = _base(template)
    for command in plan:
        name = command["operation"]
        if name not in {"u1q", "rz", "cz", "cx", "iswap", "measure", "barrier", "gphase"}:
            raise NotChecked(f"Unknown Qibolab assembler command {name}; instruction not skipped")
        item = {"op": name, "qubits": [qubit_ids.index(value) for value in command["physical_ids"]],
                "params": command["parameters"]}
        if name == "measure":
            item["clbits"] = [command["readout"]]
        ir["instructions"].append(item)
    return ir
