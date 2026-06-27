"""4-qubit Clifford-only QFT-like sketch for parity testing.

Strictly Cliffords so the spn surface grammar (which doesn't carry
parameterised rotations in our example corpus) and the Qiskit
construction stay structurally aligned.
"""
from qiskit import QuantumCircuit


def build() -> QuantumCircuit:
    qc = QuantumCircuit(4, 4)
    for i in range(4):
        qc.h(i)
        if i + 1 < 4:
            qc.cx(i, i + 1)
    qc.measure(range(4), range(4))
    return qc
