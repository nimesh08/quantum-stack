"""Small 3-qubit Clifford circuit for parity testing."""
from qiskit import QuantumCircuit


def build() -> QuantumCircuit:
    qc = QuantumCircuit(3, 3)
    qc.h(0)
    qc.cx(0, 1)
    qc.h(1)
    qc.cx(1, 2)
    qc.h(2)
    qc.cx(0, 2)
    qc.measure(range(3), range(3))
    return qc
