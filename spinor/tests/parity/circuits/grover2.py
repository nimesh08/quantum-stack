"""2-qubit Grover searching for |11>. One iteration is optimal.
Ideal output: 100% |11>."""
from qiskit import QuantumCircuit
def build():
    qc = QuantumCircuit(2, 2)
    # init superposition
    qc.h(0); qc.h(1)
    # oracle: phase-flip |11>  =  CZ
    qc.cz(0, 1)
    # diffuser
    qc.h(0); qc.h(1)
    qc.x(0); qc.x(1)
    qc.cz(0, 1)
    qc.x(0); qc.x(1)
    qc.h(0); qc.h(1)
    qc.measure([0,1], [0,1])
    return qc
