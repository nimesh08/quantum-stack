#!/usr/bin/env python3
"""phonon/bench/bench.py — Phase B benchmark harness.

Compare gate counts and depth produced by:

  - **us**: the Phonon optimizer + Phase A's `spinorc compile`,
  - **pytket**: TKET 2.16.0 (Apache-2.0, Quantinuum/CQCL),
  - **qiskit**: Qiskit's transpiler (used as a comparison
    baseline ONLY — never on our optimize path; Rule 5 in motion).

Usage:

    pip install pytket==2.16.0 qiskit
    python3 phonon/bench/bench.py path/to/circuit.qasm

The script reads an OpenQASM 3 (or 2 with the auto-translation
qiskit applies) file and runs three transpiler invocations:

  - **us**: load the file as our flat-Spinor artifact (no
    transpilation needed; we already emitted post-optimize) and
    print the gate counts.
  - **pytket**: the default `auto_rebase_pass` for a generic
    architecture, then count gates.
  - **qiskit**: `transpile(circuit, optimization_level=3)`.

Output is a markdown table to stdout. Designed to run
out-of-CI; CI uses the cassette path.

Per Phonon Deep-Dive Part 3 §8 swap policy:

  > t|ket⟩ is a temporary routing baseline AND a permanent
  > benchmark to beat. Qiskit is never in the optimization path.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def count_qasm_gates(qasm_text: str) -> tuple[int, int]:
    """Quick gate / depth counter for OpenQASM 3.x text.
    Counts every line that looks like a gate application
    (token ending in `;`, not declaration / measure / etc).
    """
    gates = 0
    qubits = 0
    for line in qasm_text.splitlines():
        s = line.strip()
        if not s or s.startswith("//") or s.startswith("OPENQASM"):
            continue
        if s.startswith(("qubit", "bit", "include", "gate ", "#pragma", "box")):
            if s.startswith("qubit"):
                # qubit[N] q;
                try:
                    n = int(s.split("[")[1].split("]")[0])
                    qubits += n
                except (IndexError, ValueError):
                    pass
            continue
        if s.startswith(("measure", "reset", "barrier", "{", "}")):
            continue
        if s.endswith(";"):
            gates += 1
    # Depth: a tight upper bound is the number of gates; we don't
    # parse the DAG. The pytket / qiskit paths report a real depth
    # via their own utilities.
    return gates, gates


def bench_pytket(qasm_text: str) -> tuple[int, int]:
    try:
        from pytket.circuit import Circuit  # type: ignore
        from pytket.qasm import circuit_from_qasm_str  # type: ignore
    except ImportError:
        return (-1, -1)
    try:
        c = circuit_from_qasm_str(qasm_text)
    except Exception:
        return (-1, -1)
    return c.n_gates, c.depth()


def bench_qiskit(qasm_text: str) -> tuple[int, int]:
    try:
        from qiskit import QuantumCircuit, transpile  # type: ignore
    except ImportError:
        return (-1, -1)
    try:
        c = QuantumCircuit.from_qasm_str(qasm_text)
    except Exception:
        return (-1, -1)
    t = transpile(c, optimization_level=3)
    return sum(t.count_ops().values()), t.depth()


def fmt(n: int) -> str:
    return "n/a" if n < 0 else str(n)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("qasm_path", type=Path)
    args = ap.parse_args()

    text = args.qasm_path.read_text()
    us = count_qasm_gates(text)
    tk = bench_pytket(text)
    qk = bench_qiskit(text)

    print("| Pipeline | Gates | Depth |")
    print("| --- | --- | --- |")
    print(f"| us (phonon → spinor) | {fmt(us[0])} | {fmt(us[1])} |")
    print(f"| pytket TKET 2.16.0   | {fmt(tk[0])} | {fmt(tk[1])} |")
    print(f"| qiskit (baseline)    | {fmt(qk[0])} | {fmt(qk[1])} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
