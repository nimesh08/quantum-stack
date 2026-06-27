"""Tiny VQE-style ansatz (Clifford-only for spn-parity)."""
from qiskit import QuantumCircuit


def build() -> QuantumCircuit:
    qc = QuantumCircuit(4, 4)
    qc.h(0)
    qc.h(1)
    qc.cx(0, 1)
    qc.cx(1, 2)
    qc.cx(2, 3)
    qc.h(2)
    qc.h(3)
    qc.cx(0, 3)
    qc.measure(range(4), range(4))
    return qc
