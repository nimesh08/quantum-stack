#!/usr/bin/env python3
"""Side-by-side Spinor vs. Qiskit transpiler comparison, using a
REAL IBM Quantum backend's target. Connects to IBM Cloud with the
user's token+CRN, fetches ibm_kingston's full target description
(coupling map, basis gates, error rates), and runs Qiskit
transpile() at optimization_level=0..3 against that target. Then
runs Spinor at -O0..-O3 on the same .spn sources and prints a
per-circuit comparison table.

Outputs:
  - per-(circuit, level) row: spn_size  qk_size  spn_depth  qk_depth
  - end-to-end: confirms our compiler can authenticate, list
    backends, and pull the live calibration target IBM serves.

Does NOT submit jobs to hardware by default. Pass --submit to also
fire a single Bell-pair shot at ibm_kingston via SamplerV2 and
report the histogram, proving end-to-end.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
CIRCUITS = ["bell", "ghz5", "qft4", "random_clifford", "vqe_ansatz"]
LEVELS = [0, 1, 2, 3]
TARGET_CHIP = "ibm_kingston"


def _spinor_bin() -> str:
    return os.environ.get(
        "SPINOR_BIN",
        "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/build/spinor/cli/spinorc",
    )


def _registry_env() -> dict:
    env = os.environ.copy()
    env.setdefault(
        "SPINOR_REGISTRY_ROOT",
        "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/spinor/registry",
    )
    return env


def spn_emit(name: str, level: int) -> str:
    spn = HERE / "circuits" / f"{name}.spn"
    cmd = [_spinor_bin(), "emit", "-t", TARGET_CHIP, "-O", str(level),
           "-f", "qasm3", "--verbatim", str(spn)]
    return subprocess.check_output(cmd, env=_registry_env(), text=True)


def _qk_circuit(name: str):
    mod_path = HERE / "circuits" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"qkc_{name}", mod_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)  # type: ignore[union-attr]
    return mod.build()


def _count_native(qasm: str) -> tuple[dict[str, int], int]:
    """Return (op_counts, depth_estimate). Depth is approximated as
    op count for the comparison (Spinor's emit pass doesn't carry
    a depth field; Qiskit's QuantumCircuit.depth() is exact)."""
    counts: dict[str, int] = {}
    in_box = False
    total = 0
    for raw in qasm.splitlines():
        line = raw.strip()
        if line.startswith("box"):
            in_box = True
            continue
        if line.startswith("}") and in_box:
            in_box = False
            continue
        if not in_box or not line:
            continue
        if "= measure" in line or line.startswith("measure"):
            counts["measure"] = counts.get("measure", 0) + 1
            total += 1
            continue
        head = line.split("(")[0].split(" ")[0].rstrip(";")
        if head in ("c", "//"):
            continue
        counts[head] = counts.get(head, 0) + 1
        total += 1
    return counts, total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--token", default=os.environ.get("IBM_QUANTUM_TOKEN"))
    parser.add_argument("--instance", default=os.environ.get("IBM_QUANTUM_INSTANCE"))
    parser.add_argument("--backend", default=TARGET_CHIP)
    parser.add_argument("--submit", action="store_true",
                        help="Submit one Bell-pair shot to IBM hardware")
    args = parser.parse_args()
    if not args.token or not args.instance:
        print("ERROR: pass --token + --instance (or set IBM_QUANTUM_TOKEN /"
              " IBM_QUANTUM_INSTANCE env vars).", file=sys.stderr)
        return 2

    from qiskit import transpile
    from qiskit_ibm_runtime import QiskitRuntimeService

    print(f"# Connecting to IBM Cloud, instance = {args.instance[:60]}...")
    service = QiskitRuntimeService(channel="ibm_cloud",
                                   token=args.token,
                                   instance=args.instance)
    print(f"# Authenticated. Listing operational backends...")
    backends = service.backends(simulator=False, operational=True)
    print(f"# Found {len(backends)} operational hardware backends:")
    for b in backends:
        proc = getattr(b, "processor_type", None)
        print(f"#   - {b.name}  (qubits={b.num_qubits}, type={proc})")

    print(f"\n# Loading target backend: {args.backend}")
    try:
        backend = service.backend(args.backend)
    except Exception as e:
        print(f"ERROR: cannot load backend '{args.backend}': {e}")
        return 1
    print(f"#   basis_gates = {backend.configuration().basis_gates}")
    print(f"#   num_qubits  = {backend.num_qubits}")
    print(f"#   processor   = {backend.processor_type if hasattr(backend, 'processor_type') else 'n/a'}")

    print(f"\n# Comparison table (spinor vs qiskit) on {args.backend}\n")
    header = f"{'circuit':<18} {'lvl':>4} | {'spn_size':>9} {'qk_size':>8} | {'spn_2q':>7} {'qk_2q':>6} | {'qk_depth':>8} | verdict"
    print(header)
    print("-" * len(header))

    failures = []
    rows = []
    for name in CIRCUITS:
        for level in LEVELS:
            try:
                spn_qasm = spn_emit(name, level)
            except subprocess.CalledProcessError as e:
                print(f"FAIL spn emit {name} -O{level}: rc={e.returncode}")
                failures.append((name, level, "spn-emit-failed"))
                continue
            spn_counts, spn_size = _count_native(spn_qasm)
            spn_2q = spn_counts.get("cz", 0) + spn_counts.get("ecr", 0)

            qc = _qk_circuit(name)
            try:
                qt = transpile(qc, backend=backend, optimization_level=level,
                               seed_transpiler=42)
            except Exception as e:  # noqa: BLE001
                print(f"FAIL qiskit transpile {name} O{level}: {e}")
                failures.append((name, level, "qk-transpile-failed"))
                continue
            qk_counts = {k: int(v) for k, v in qt.count_ops().items()}
            qk_size = sum(qk_counts.values())
            qk_2q = qk_counts.get("cz", 0) + qk_counts.get("ecr", 0)
            qk_depth = qt.depth()

            # Gate-set check: every Spinor op must be in IBM's basis.
            allowed = set(backend.configuration().basis_gates) | {"measure", "barrier"}
            unknown = [g for g in spn_counts if g not in allowed]
            verdict = "OK"
            if unknown:
                verdict = f"FAIL non-native {unknown}"
                failures.append((name, level, verdict))

            rows.append((name, level, spn_size, qk_size, spn_2q, qk_2q, qk_depth, verdict))
            print(f"{name:<18} {level:>4} | {spn_size:>9} {qk_size:>8} | {spn_2q:>7} {qk_2q:>6} | {qk_depth:>8} | {verdict}")

    print()
    if failures:
        print(f"# {len(failures)} failure(s):")
        for f in failures:
            print(f"#   {f}")
        return 1
    print(f"# all {len(rows)} cases compiled cleanly")

    if args.submit:
        print(f"\n# --- Live submission to {args.backend} (Bell pair, 1024 shots) ---")
        from qiskit_ibm_runtime import SamplerV2
        qc = _qk_circuit("bell")
        qt = transpile(qc, backend=backend, optimization_level=3, seed_transpiler=42)
        sampler = SamplerV2(mode=backend)
        job = sampler.run([qt], shots=1024)
        print(f"# job id: {job.job_id()}")
        print(f"# waiting for completion (this can queue for a few minutes)...")
        result = job.result()
        counts = result[0].data.c.get_counts()
        print(f"# histogram: {dict(counts)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
