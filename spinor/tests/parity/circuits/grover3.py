"""3-qubit Grover searching for |101> (single iteration).
Theoretical success probability with 1 iteration on 3 qubits: ~78%.
Two iterations would lower it (over-rotation) — so 1 iteration is the
sweet spot for N=8, M=1."""
from qiskit import QuantumCircuit
import numpy as np
def build():
    qc = QuantumCircuit(3, 3)
    qc.h([0, 1, 2])
    # Oracle for |101>: X on q1, then CCZ, then X on q1
    qc.x(1)
    qc.h(2); qc.ccx(0, 1, 2); qc.h(2)
    qc.x(1)
    # Diffuser
    qc.h([0, 1, 2])
    qc.x([0, 1, 2])
    qc.h(2); qc.ccx(0, 1, 2); qc.h(2)
    qc.x([0, 1, 2])
    qc.h([0, 1, 2])
    qc.measure(range(3), range(3))
    return qc
