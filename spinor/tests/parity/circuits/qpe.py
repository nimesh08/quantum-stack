"""3-bit QPE of U = T (phase π/4) on eigenstate |1>.
Ideal phase = 1/8 (since T = e^{iπ/4} on |1>, so phase 1/8 of full 2π).
With 3 counting qubits, result should be |001> (binary 001 = 1/8) with high prob.
"""
from qiskit import QuantumCircuit
import numpy as np
def build():
    # 3 counting qubits + 1 eigenstate qubit
    qc = QuantumCircuit(4, 3)
    # Prepare eigenstate |1>
    qc.x(3)
    # Counting register in superposition
    qc.h([0, 1, 2])
    # Controlled-U^{2^k} for k=0,1,2  (U = T)
    qc.cp(np.pi/4, 2, 3)              # 1 application
    qc.cp(np.pi/4, 1, 3); qc.cp(np.pi/4, 1, 3)  # 2 applications
    for _ in range(4):
        qc.cp(np.pi/4, 0, 3)         # 4 applications
    # Inverse QFT on counting register
    qc.h(2)
    qc.cp(-np.pi/2, 1, 2)
    qc.h(1)
    qc.cp(-np.pi/4, 0, 2)
    qc.cp(-np.pi/2, 0, 1)
    qc.h(0)
    # Measure counting register only
    qc.measure([0, 1, 2], [0, 1, 2])
    return qc
