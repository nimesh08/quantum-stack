"""Independent operators/instruments for the actual SDK transport objects."""
import importlib
import os
import socket

import pytest

from qstack.models import CompiledArtifact
from qstack.providers.native import cirq_circuit, qiskit_circuit
from qstack.verification import NotChecked
from qstack.verification.engine import compare
from qstack.verification.sdk import cirq_object, qiskit_object


@pytest.fixture(autouse=True)
def no_network(monkeypatch):
    def fail(*args, **kwargs):
        pytest.fail("Independent SDK checks must remain offline")
    for name in ("connect", "connect_ex"):
        monkeypatch.setattr(socket.socket, name, fail)
    monkeypatch.setattr(socket, "create_connection", fail)


def sdk(route, name):
    selected = os.environ.get("QSTACK_TEST_SDK")
    if selected and selected != route:
        pytest.skip(f"SDK matrix selected {selected}")
    return importlib.import_module(name) if selected else pytest.importorskip(name)


def artifact(route, instructions):
    return CompiledArtifact(route, "offline", "json", "{}", {
        "schema_version": 1, "num_qubits": 2, "num_clbits": 3,
        "global_phase": 0.237, "instructions": instructions,
        "measurement_mapping": [{"qubit": 0, "clbit": 2}, {"qubit": 1, "clbit": 0}],
    }, target_snapshot={"qubit_labels": ["5_2", "5_3"]})


def check(expected, decoded, *, phase_loss=False):
    return compare(expected, decoded, max_qubits=4, max_paths=64, threshold=2e-13,
                   allow_scalar_phase_loss=phase_loss)


@pytest.mark.parametrize("dynamic", [False, True])
def test_qiskit_objects_full_operators_and_instruments(dynamic, monkeypatch):
    qiskit = sdk("ibm", "qiskit")
    monkeypatch.setattr(qiskit, "transpile", lambda *a, **k: pytest.fail("Provider transpiler invoked"))
    operations = [{"op": "rz", "qubits": [0], "params": [0.37]}, {"op": "sx", "qubits": [0]},
                  {"op": "ecr", "qubits": [1, 0]}, {"op": "cx", "qubits": [0, 1]}]
    if dynamic:
        operations += [{"op": "measure", "qubits": [0], "clbits": [2]},
                       {"op": "if", "clbits": [2], "condition_value": 1},
                       {"op": "x", "qubits": [1]}, {"op": "gphase", "params": [0.4]},
                       {"op": "else"}, {"op": "reset", "qubits": [1]}, {"op": "endif"},
                       {"op": "measure", "qubits": [1], "clbits": [0]},
                       {"op": "rx", "qubits": [1], "params": [0.24]}]
    compiled = artifact("ibm", operations)
    circuit = qiskit_circuit(compiled)
    assert check(compiled.physical_ir, qiskit_object(circuit, compiled.physical_ir))["status"] == "passed"
    # A wrong angle is detected independently of the serializer's own tests.
    broken = circuit.copy()
    if dynamic:
        broken.data[-1] = broken.data[-1].replace(operation=qiskit.circuit.library.RXGate(0.34))
    else:
        broken.data[0] = broken.data[0].replace(operation=qiskit.circuit.library.RZGate(0.47))
    assert check(compiled.physical_ir, qiskit_object(broken, compiled.physical_ir))["status"] == "failed"


def test_qiskit_sdk_mutated_operands_and_readouts_are_detected():
    qiskit = sdk("ibm", "qiskit")
    compiled = artifact("ibm", [{"op": "cx", "qubits": [0, 1]},
        {"op": "measure", "qubits": [0], "clbits": [2]}, {"op": "measure", "qubits": [1], "clbits": [0]}])
    circuit = qiskit_circuit(compiled)
    for mutation in ("operands", "drop_readout", "exchange_readout"):
        broken = circuit.copy()
        if mutation == "operands":
            broken.data[0] = broken.data[0].replace(qubits=tuple(reversed(broken.data[0].qubits)))
        elif mutation == "drop_readout":
            del broken.data[-1]
        else:
            broken.data[-1] = broken.data[-1].replace(clbits=(broken.clbits[1],))
        assert check(compiled.physical_ir, qiskit_object(broken, compiled.physical_ir))["status"] == "failed", mutation
    broken = circuit.copy()
    broken.append(qiskit.circuit.Instruction("unknown", 1, 0, []), [0])
    with pytest.raises(NotChecked, match="not skipped"):
        qiskit_object(broken, compiled.physical_ir)


@pytest.mark.parametrize("measured", [False, True])
def test_cirq_sdk_objects_have_independent_native_semantics(measured, monkeypatch):
    cirq = sdk("google", "cirq")
    sdk("google", "cirq_google")
    instructions = [{"op": "phased_xz", "qubits": [0], "params": [0.27, -0.41, 0.62]},
                    {"op": "sqrt_iswap_inv", "qubits": [0, 1]}, {"op": "syc", "qubits": [1, 0]}]
    if measured:
        instructions += [{"op": "measure", "qubits": [0], "clbits": [2]},
                         {"op": "reset", "qubits": [0]},
                         {"op": "measure", "qubits": [1], "clbits": [0]}]
    compiled = artifact("google", instructions)
    circuit = cirq_circuit(compiled, {})
    labels = compiled.target_snapshot["qubit_labels"]
    assert check(compiled.physical_ir, cirq_object(circuit, compiled.physical_ir, labels), phase_loss=True)["status"] == "passed"
    broken = cirq.Circuit(circuit)
    broken.append(cirq.X(cirq.GridQubit(5, 2)))
    assert check(compiled.physical_ir, cirq_object(broken, compiled.physical_ir, labels), phase_loss=True)["status"] == "failed"
    broken.append(cirq.amplitude_damp(0.1)(cirq.GridQubit(5, 2)))
    with pytest.raises(NotChecked, match="not skipped"):
        cirq_object(broken, compiled.physical_ir, labels)


def test_cirq_asymmetric_operand_order_and_readout_mutations():
    cirq = sdk("google", "cirq")
    compiled = artifact("google", [{"op": "cx", "qubits": [0, 1]},
        {"op": "measure", "qubits": [0], "clbits": [2]}, {"op": "measure", "qubits": [1], "clbits": [0]}])
    a, b = cirq.GridQubit(5, 2), cirq.GridQubit(5, 3)
    labels = compiled.target_snapshot["qubit_labels"]
    correct = cirq.Circuit(cirq.CNOT(a, b), cirq.measure(a, key="c2"), cirq.measure(b, key="c0"))
    assert check(compiled.physical_ir, cirq_object(correct, compiled.physical_ir, labels), phase_loss=True)["status"] == "passed"
    for wrong in (cirq.Circuit(cirq.CNOT(b, a), cirq.measure(a, key="c2"), cirq.measure(b, key="c0")),
                  cirq.Circuit(cirq.CNOT(a, b), cirq.measure(a, key="c2")),
                  cirq.Circuit(cirq.CNOT(a, b), cirq.measure(a, key="c1"), cirq.measure(b, key="c0"))):
        assert check(compiled.physical_ir, cirq_object(wrong, compiled.physical_ir, labels), phase_loss=True)["status"] == "failed"
