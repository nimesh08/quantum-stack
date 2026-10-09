"""Optional independent matrix oracle; requires numpy + qiskit, never transpiles.

Run: python sdk_oracle.py --spinorc /path/to/spinorc
Temporary source/registry files remain inside the supplied work directory.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from qiskit import QuantumCircuit, qasm3
from qiskit.circuit.library import XXPlusYYGate
from qiskit.quantum_info import Operator


def circuit_from_native(ir: dict) -> QuantumCircuit:
    qc = QuantumCircuit(2)
    qc.global_phase = ir["global_phase"]
    assert ir["logical_to_physical"] == [0, 1]
    assert ir["initial_logical_to_physical"] == [0, 1]
    for inst in ir["instructions"]:
        op, qs, ps = inst["op"], inst["qubits"], inst["params"]
        if op in {"barrier", "measure"}:
            continue
        if op == "u1q":
            qc.r(*ps, qs[0])
        elif op in {"gpi", "gpi2"}:
            qc.r(np.pi if op == "gpi" else np.pi / 2, ps[0], qs[0])
            if op == "gpi":
                qc.global_phase += np.pi / 2
        elif op == "phased_xz":
            x, z, axis = ps
            qc.rz(-axis, qs[0])
            qc.rx(x, qs[0])
            qc.rz(z + axis, qs[0])
            qc.global_phase += (x + z) / 2
        elif op in {"sqrt_iswap", "sqrt_iswap_inv"}:
            theta = -np.pi / 2 if op == "sqrt_iswap" else np.pi / 2
            qc.append(XXPlusYYGate(theta), qs)
        elif op == "syc":
            qc.append(XXPlusYYGate(np.pi), qs)
            qc.cp(-np.pi / 6, *qs)
        elif op == "ms":
            qc.rxx(np.pi / 2, *qs)
        else:
            getattr(qc, op)(*ps, *qs)
    return qc


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spinorc", required=True, type=Path)
    parser.add_argument("--work", type=Path, default=Path("build"))
    args = parser.parse_args()
    binary = args.spinorc.resolve()
    cases = [("rz, sx", gate) for gate in ("cx", "cz", "ecr")]
    cases += [("gpi, gpi2", gate) for gate in ("ms", "rzz")]
    cases += [("u1q, rz", gate) for gate in ("rxx", "rzz")]
    cases += [("phased_xz", gate) for gate in ("cz", "sqrt_iswap", "sqrt_iswap_inv", "syc")]
    cases += [("rx, rz", "iswap")]
    args.work.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(943)
    total = 0
    qasm_total = 0
    worst = 0.0
    with tempfile.TemporaryDirectory(prefix="sdk-oracle-", dir=args.work) as tmp:
        root = Path(tmp).resolve()
        (root / "chips").mkdir()
        env = dict(os.environ, SPINOR_REGISTRY_ROOT=str(root))
        for index, (oneq, entangler) in enumerate(cases):
            target = f"oracle_{index}"
            (root / "chips" / f"{target}.yaml").write_text(
                f"id: {target}\nprovider: local\nqubits: 2\n"
                f"native_gates: [{oneq}, {entangler}]\n"
                "coupling_map:\n  topology: all_to_all\n  size: 2\n"
                "decomposition:\n  one_qubit:\n    recipe: euler_zyz\n    rotation_gate: rz\n"
                + ("    pi_2_gate: rx\n" if entangler == "iswap" else "")
                + f"  two_qubit:\n    recipe: kak\n    entangler: {entangler}\n    entangler_count_max: 3\n",
                encoding="utf-8",
            )
            for trial in range(3):
                a, b, c, d, e = rng.uniform(-12, 12, size=5)
                source = root / "input.spn"
                source.write_text(
                    f"target generic\nqubit q[2]\nrx({a}) q[0]\nry({b}) q[1]\n"
                    f"rz({c}) q[0]\nh q[1]\ncx q[0], q[1]\n"
                    f"rzz({d}) q[1], q[0]\nrxx({e}) q[0], q[1]\necr q[0], q[1]\n",
                    encoding="utf-8",
                )
                original = QuantumCircuit(2)
                original.rx(a, 0)
                original.ry(b, 1)
                original.rz(c, 0)
                original.h(1)
                original.cx(0, 1)
                original.rzz(d, 1, 0)
                original.rxx(e, 0, 1)
                original.ecr(0, 1)
                expected = Operator(original).data
                for level in (0, 3):
                    result = subprocess.run(
                        [str(binary), "emit", "-t", target, "-f", "json", "-O", str(level), str(source)],
                        env=env, capture_output=True, text=True,
                    )
                    if result.returncode:
                        raise RuntimeError(result.stderr)
                    actual = Operator(circuit_from_native(json.loads(result.stdout))).data
                    error = float(np.max(np.abs(expected - actual)))
                    assert error < 2e-8, (target, trial, level, error)
                    worst = max(worst, error)
                    total += 1
                    if trial == 0 and level == 0:
                        assembly = subprocess.run(
                            [str(binary), "emit", "-t", target, "-f", "qasm3", "-O", "0", str(source)],
                            env=env, capture_output=True, text=True, check=True,
                        ).stdout
                        emitted = Operator(qasm3.loads(assembly)).data
                        error = float(np.max(np.abs(expected - emitted)))
                        assert error < 2e-8, (target, "QASM3", error)
                        worst = max(worst, error)
                        qasm_total += 1
    print(f"Qiskit matrix oracle: {total} native JSON and {qasm_total} QASM3 circuits passed; max absolute error {worst:.3g}")


if __name__ == "__main__":
    main()
