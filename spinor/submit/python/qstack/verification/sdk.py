"""Independent interpretation of actual SDK objects, without SDK simulation.

Gate-local public SDK matrices define the transported instruction semantics.
The complete operator/instrument is assembled by our independent oracle. No
compiler matrix, serializer, transpiler or simulator supplies the expectation.
Unsupported nonunitary operations fail closed instead of being skipped.
"""
from __future__ import annotations

import copy
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
    ir = _base(template)
    if circuit.num_qubits != ir["num_qubits"] or circuit.num_clbits != ir["num_clbits"]:
        raise ValueError("SDK circuit register sizes differ from the compiled interface")

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


def cirq_object(circuit, template, qubit_labels):
    """Read native Cirq operations, including repeated readout-key writes."""
    import cirq
    ir = _base(template)
    labels = [tuple(map(int, label.split("_"))) if isinstance(label, str) else tuple(label) for label in qubit_labels]
    mapping = {cirq.GridQubit(*label): index for index, label in enumerate(labels)}
    for operation in circuit.all_operations():
        try:
            qubits = [mapping[q] for q in operation.qubits]
        except KeyError as exc:
            raise ValueError("SDK instruction uses a qubit absent from its target snapshot") from exc
        if cirq.is_measurement(operation):
            match = re.fullmatch(r"c(\d+)(?:__\d+)?", cirq.measurement_key_name(operation))
            if not match or len(qubits) != 1 or any(operation.gate.invert_mask):
                raise NotChecked("Only the documented one-bit SDK readout contract is covered")
            bit = int(match[1])
            if not 0 <= bit < ir["num_clbits"]:
                raise ValueError("SDK measurement output exceeds the declared register")
            ir["instructions"].append({"op": "measure", "qubits": qubits, "clbits": [bit]})
        elif isinstance(operation.gate, cirq.ResetChannel) and len(qubits) == 1:
            ir["instructions"].append({"op": "reset", "qubits": qubits})
        else:
            local = cirq.unitary(operation, default=None)
            if local is None:
                raise NotChecked(f"No independent SDK operator for {operation}; instruction not skipped")
            # Cirq and this oracle number the first local operand most-significantly.
            ir["instructions"].append({"op": "sdk_unitary", "qubits": qubits, "matrix": local})
    return ir
