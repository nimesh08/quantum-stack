from qiskit import QuantumCircuit
def build():
    qc = QuantumCircuit(4, 4)
    qc.h(0)
    qc.cx(0, 1)
    qc.cx(1, 2)
    qc.cx(2, 3)
    qc.measure(range(4), range(4))
    return qc
