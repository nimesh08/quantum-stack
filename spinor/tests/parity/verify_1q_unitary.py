#!/usr/bin/env python3
"""Offline statevector check for single-qubit gate lowering.

For each named gate in {H, X, Y, Z, S, Sdg, T, Tdg} and each -O
level in {0, 1, 2, 3}, write a tiny .spn with exactly that gate
applied to one qubit, compile via spinorc, parse the emitted
sequence, multiply the matrices, and verify the result equals the
input gate up to a global phase.

This catches angle-table and ZYZ-substitution bugs WITHOUT needing
IBM hardware credentials. Exit 0 on all-pass; non-zero on any
mismatch.
"""
from __future__ import annotations
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

PI = np.pi
SPINOR_BIN = os.environ.get(
    "SPINOR_BIN",
    "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/build/spinor/cli/spinorc",
)
REGISTRY_ROOT = os.environ.get(
    "SPINOR_REGISTRY_ROOT",
    "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/spinor/registry",
)
TARGET = os.environ.get("SPINOR_TARGET", "ibm_kingston")


def Rz(t): return np.array([[np.exp(-1j*t/2), 0], [0, np.exp(1j*t/2)]], dtype=complex)
def Rx(t): return np.array([[np.cos(t/2), -1j*np.sin(t/2)], [-1j*np.sin(t/2), np.cos(t/2)]], dtype=complex)
def Ry(t): return np.array([[np.cos(t/2), -np.sin(t/2)], [np.sin(t/2), np.cos(t/2)]], dtype=complex)
SX = 0.5 * np.array([[1+1j, 1-1j], [1-1j, 1+1j]], dtype=complex)
SXDG = SX.conj().T
X = np.array([[0, 1], [1, 0]], dtype=complex)
Y = np.array([[0, -1j], [1j, 0]], dtype=complex)
Z = np.array([[1, 0], [0, -1]], dtype=complex)
H = (1/np.sqrt(2)) * np.array([[1, 1], [1, -1]], dtype=complex)
S = np.array([[1, 0], [0, 1j]], dtype=complex)
SDG = S.conj().T
T = np.array([[1, 0], [0, np.exp(1j*PI/4)]], dtype=complex)
TDG = T.conj().T
I = np.eye(2, dtype=complex)

GATES = {
    "h":   H,
    "x":   X,
    "y":   Y,
    "z":   Z,
    "s":   S,
    "sdg": SDG,
    "t":   T,
    "tdg": TDG,
}


def norm(M):
    if abs(M[0, 0]) > 1e-9:
        return M / (M[0, 0] / abs(M[0, 0]))
    if abs(M[0, 1]) > 1e-9:
        return M / (M[0, 1] / abs(M[0, 1]))
    return M


def emit(gate_name: str, level: int) -> str:
    spn = f"target generic\nqubit q[1]\nbit c[1]\n{gate_name} q[0]\nc = measure q\n"
    with tempfile.NamedTemporaryFile(suffix=".spn", mode="w", delete=False) as f:
        f.write(spn)
        path = f.name
    try:
        env = os.environ.copy()
        env["SPINOR_REGISTRY_ROOT"] = REGISTRY_ROOT
        return subprocess.check_output(
            [SPINOR_BIN, "emit", "-t", TARGET, "-O", str(level),
             "-f", "qasm3", path],
            env=env, text=True,
        )
    finally:
        os.unlink(path)


def apply_qasm(qasm: str) -> np.ndarray:
    """Multiply the gate sequence (excluding measure) into a single 2×2 U."""
    U = I.copy()
    for raw in qasm.splitlines():
        line = raw.strip().rstrip(";")
        if not line or line.startswith(("OPENQASM", "include", "qubit", "bit", "//")):
            continue
        if "= measure" in line or line.startswith("measure"):
            continue
        m = re.match(r"rz\(([^)]+)\)\s+q\[(\d+)\]", line)
        if m:
            U = Rz(float(m.group(1))) @ U
            continue
        m = re.match(r"(sx|sxdg|x|y|z|s|sdg|t|tdg|id|h)\s+q\[(\d+)\]", line)
        if m:
            tag = m.group(1)
            mat = {"sx": SX, "sxdg": SXDG, "x": X, "y": Y, "z": Z,
                   "s": S, "sdg": SDG, "t": T, "tdg": TDG, "id": I, "h": H}[tag]
            U = mat @ U
            continue
        print(f"  ! skipped: {line}", file=sys.stderr)
    return U


def main() -> int:
    print(f"# verify_1q_unitary.py  (target={TARGET}, bin={SPINOR_BIN})")
    fails = 0
    for name, ref in GATES.items():
        for level in [0, 1, 2, 3]:
            try:
                qasm = emit(name, level)
            except subprocess.CalledProcessError as e:
                print(f"FAIL  {name:<4} -O{level}: spinorc returned {e.returncode}")
                fails += 1
                continue
            U = apply_qasm(qasm)
            eq = np.allclose(norm(U), norm(ref), atol=1e-9)
            verdict = "OK" if eq else "MISMATCH"
            if not eq:
                fails += 1
            print(f"{verdict:<8} {name:<4} -O{level}")
            if not eq:
                # Print the emitted sequence and the matrix delta for diagnosis.
                print("  emitted:")
                for line in qasm.splitlines():
                    s = line.strip()
                    if s and not s.startswith(("OPENQASM", "include", "qubit", "bit", "//", "box", "}", "{")):
                        print(f"    {s}")
                print(f"  emitted matrix (normalized):\n{np.round(norm(U), 4)}")
                print(f"  reference   (normalized):\n{np.round(norm(ref), 4)}")
    print()
    print(f"# summary: {fails} failure(s)")
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
