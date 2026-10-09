"""Independent elementary ket definitions, deliberately separate from synthesis."""
import math
import numpy as np
from . import NotChecked

I = np.eye(2, dtype=complex)
X = np.array([[0, 1], [1, 0]], dtype=complex)
Y = np.array([[0, -1j], [1j, 0]], dtype=complex)
Z = np.diag([1, -1]).astype(complex)


def rotation(angle, axis):
    return math.cos(angle / 2) * np.eye(len(axis)) - 1j * math.sin(angle / 2) * axis


def matrix(item):
    g, p = item["op"].lower(), item.get("params", [])
    if "matrix" in item:
        m = np.asarray([[complex(*v) if isinstance(v, list) else complex(v) for v in row] for row in item["matrix"]])
        if m.shape != (1 << len(item["qubits"]),) * 2 or not np.all(np.isfinite(m)):
            raise ValueError("Invalid explicit gate matrix")
        return m
    if g in {"id", "i", "h", "x", "y", "z"}:
        return {"id": I, "i": I, "h": (X + Z) / math.sqrt(2), "x": X, "y": Y, "z": Z}[g]
    if g in {"s", "sdg", "t", "tdg"}:
        return np.diag([1, np.exp(1j * {"s": math.pi / 2, "sdg": -math.pi / 2, "t": math.pi / 4, "tdg": -math.pi / 4}[g])])
    if g in {"sx", "sxdg"}:
        sx = ((1 + 1j) * I + (1 - 1j) * X) / 2
        return sx if g == "sx" else sx.conj().T
    if g in {"rx", "ry", "rz"}:
        return rotation(p[0], {"rx": X, "ry": Y, "rz": Z}[g])
    if g == "p":
        return np.diag([1, np.exp(1j * p[0])])
    if g in {"u1q", "prx", "r"}:
        return rotation(p[0], math.cos(p[1]) * X + math.sin(p[1]) * Y)
    if g == "phased_xz":
        x, z, a = p
        return np.exp(.5j * (x + z)) * rotation(a + z, Z) @ rotation(x, X) @ rotation(-a, Z)
    if g in {"gpi", "gpi2"}:
        axis = math.cos(p[0]) * X + math.sin(p[0]) * Y
        return axis if g == "gpi" else rotation(math.pi / 2, axis)
    if g in {"cz", "cp"}:
        return np.diag([1, 1, 1, -1 if g == "cz" else np.exp(1j * p[0])]).astype(complex)
    if g in {"cx", "cnot"}:
        return np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1], [0, 0, 1, 0]], complex)
    if g == "swap":
        return np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], complex)
    if g in {"rxx", "ryy", "rzz", "xx", "yy", "zz"}:
        a = {"x": X, "y": Y, "z": Z}[g[-1]]
        return rotation(p[0], np.kron(a, a))
    if g == "ms":
        if not p:
            return rotation(math.pi / 2, np.kron(X, X))
        a, b, angle = (*p, math.pi / 2) if len(p) == 2 else p
        return rotation(angle, np.kron(math.cos(a) * X + math.sin(a) * Y, math.cos(b) * X + math.sin(b) * Y))
    if g in {"iswap", "sqrt_iswap", "sqrt_iswap_inv", "syc"}:
        t = {"iswap": -math.pi / 2, "sqrt_iswap": -math.pi / 4, "sqrt_iswap_inv": math.pi / 4, "syc": math.pi / 2}[g]
        m = np.eye(4, dtype=complex)
        m[1:3, 1:3] = [[math.cos(t), -1j * math.sin(t)], [-1j * math.sin(t), math.cos(t)]]
        if g == "syc":
            m[3, 3] = np.exp(-1j * math.pi / 6)
        return m
    if g == "ecr":
        return np.array([[0, 0, 1, 1j], [0, 0, 1j, 1], [1, -1j, 0, 0], [-1j, 1, 0, 0]], complex) / math.sqrt(2)
    if g == "move":
        # Arbitrary but fixed resonator phase checks its cancellation. The |11>
        # sector is undefined and checked for zero occupation by the interpreter.
        a = np.exp(.173j)
        return np.array([[1, 0, 0, 0], [0, 0, 1/a, 0], [0, a, 0, 0], [0, 0, 0, 0]], complex)
    raise NotChecked(f"Independent matrix definition unavailable for instruction '{g}'; instruction was not skipped")


def apply(matrix, qubits, operator, width):
    if len(set(qubits)) != len(qubits) or any(q < 0 or q >= width for q in qubits):
        raise ValueError("Invalid or repeated quantum operand")
    result = np.zeros_like(operator)
    mask = sum(1 << q for q in qubits)
    for base in range(1 << width):
        if base & mask:
            continue
        rows = [base + sum(((b >> (len(qubits) - 1 - k)) & 1) << q for k, q in enumerate(qubits)) for b in range(1 << len(qubits))]
        result[rows, :] = matrix @ operator[rows, :]
    return result
