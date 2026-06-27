"""5-qubit GHZ state for parity testing."""
from qiskit import QuantumCircuit


def build() -> QuantumCircuit:
    qc = QuantumCircuit(5, 5)
    qc.h(0)
    for i in range(4):
        qc.cx(i, i + 1)
    qc.measure(range(5), range(5))
    return qc
