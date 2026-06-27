#!/usr/bin/env python3
"""Parity test: spinorc run vs Qiskit-direct on ibm_fez.

For each test circuit (bell, ghz4, grover2, grover3):
  1. Invoke `spinorc run -t ibm_fez -O 3 --shots 1024 <name>.spn`.
     This compiles the Spinor source to Qiskit Python and spawns
     the Qiskit venv to transpile + submit, capturing the
     histogram JSON.
  2. In parallel, build the Qiskit-direct version of the same
     circuit (via circuits/<name>.py) and submit it identically.
  3. Compute fidelity (sum of shots on ideal target bitstrings)
     for both paths.
  4. Print side-by-side table.

Both paths share the same IBM credentials passed on the command
line. Both submit to ibm_fez. Differences in output reflect the
quality of the Spinor source-to-source step (which is now a
straight Spinor IR -> Qiskit Python mapping — no Spinor decompose
or cleanup runs).
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
SPINOR_BIN = os.environ.get(
    "SPINOR_BIN",
    "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/build/spinor/cli/spinorc",
)
REGISTRY_ROOT = os.environ.get(
    "SPINOR_REGISTRY_ROOT",
    "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/spinor/registry",
)

CASES = [
    {
        "name": "bell", "qubits": 2, "shots": 1024,
        "ideal": {"00": 0.5, "11": 0.5},
        "desc": "Bell pair: 50% |00>, 50% |11>",
    },
    {
        "name": "ghz4", "qubits": 4, "shots": 1024,
        "ideal": {"0000": 0.5, "1111": 0.5},
        "desc": "4-qubit GHZ: 50% |0000>, 50% |1111>",
    },
    {
        "name": "grover2", "qubits": 2, "shots": 1024,
        "ideal": {"11": 1.0},
        "desc": "2-qubit Grover, target |11>: 100% |11>",
    },
    {
        "name": "grover3", "qubits": 3, "shots": 1024,
        "ideal": {"101": 0.781},
        "desc": "3-qubit Grover, target |101>: ~78% |101>",
    },
]


def spinorc_run(name: str, target: str, level: int, shots: int,
                token: str, instance: str) -> dict:
    """Invoke `spinorc run` and parse its JSON stdout."""
    spn = HERE / "circuits" / f"{name}.spn"
    env = os.environ.copy()
    env["SPINOR_REGISTRY_ROOT"] = REGISTRY_ROOT
    cmd = [
        SPINOR_BIN, "run",
        "-t", target,
        "-O", str(level),
        "--shots", str(shots),
        "--token", token,
        "--instance", instance,
        str(spn),
    ]
    print(f"  $ {' '.join(cmd[:-1])} <{name}.spn>", file=sys.stderr)
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True,
                          timeout=900)
    if proc.returncode != 0:
        raise RuntimeError(
            f"spinorc run rc={proc.returncode}: {proc.stderr[-1000:]}"
        )
    # Find the JSON line.
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and "\"histogram\"" in line:
            return json.loads(line)
    raise RuntimeError(
        f"no histogram JSON in stdout. raw:\n{proc.stdout[-1000:]}"
    )


def qiskit_direct(name: str, backend, level: int, shots: int) -> dict:
    """Build via circuits/<name>.py, transpile, submit, return same shape."""
    from qiskit import transpile
    from qiskit_ibm_runtime import SamplerV2
    p = HERE / "circuits" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"qkc_{name}", p)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    qc = mod.build()
    qt = transpile(qc, backend=backend, optimization_level=level,
                   seed_transpiler=42)
    sampler = SamplerV2(mode=backend)
    job = sampler.run([qt], shots=shots)
    result = job.result()
    counts = result[0].data.c.get_counts()
    return {
        "histogram": {str(k): int(v) for k, v in counts.items()},
        "job_id": job.job_id(),
        "depth": qt.depth(),
        "gates": {k: int(v) for k, v in qt.count_ops().items()},
        "optimization_level": level,
    }


def fidelity(counts: dict, ideal: dict, total: int) -> float:
    """Sum of probability mass on bitstrings the ideal cares about."""
    on_target = [k for k, v in ideal.items() if v > 0.5 / 2**len(k)]
    s = sum(counts.get(k, 0) for k in on_target)
    return s / total if total > 0 else 0.0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token", required=True)
    ap.add_argument("--instance", required=True)
    ap.add_argument("--backend", default="ibm_fez")
    ap.add_argument("--level", type=int, default=3)
    args = ap.parse_args()

    from qiskit_ibm_runtime import QiskitRuntimeService
    service = QiskitRuntimeService(channel="ibm_cloud",
                                   token=args.token, instance=args.instance)
    backend = service.backend(args.backend)
    print(f"# backend: {args.backend} ({backend.configuration().basis_gates})")
    print(f"# -O level: {args.level}")
    print()

    results = []
    for case in CASES:
        name = case["name"]
        print(f"\n## {name} — {case['desc']}")
        try:
            spn = spinorc_run(name, args.backend, args.level, case["shots"],
                              args.token, args.instance)
        except Exception as e:
            print(f"  SPINOR FAILED: {e}")
            continue
        try:
            qkd = qiskit_direct(name, backend, args.level, case["shots"])
        except Exception as e:
            print(f"  QISKIT FAILED: {e}")
            continue
        spn_fid = fidelity(spn.get("histogram", {}), case["ideal"], case["shots"])
        qk_fid  = fidelity(qkd.get("histogram", {}), case["ideal"], case["shots"])
        ideal_fid = sum(v for v in case["ideal"].values())
        results.append({
            "case": case, "spn": spn, "qkd": qkd,
            "spn_fid": spn_fid, "qk_fid": qk_fid, "ideal_fid": ideal_fid,
        })
        print(f"  spinorc run job: {spn.get('job_id')}  fidelity={spn_fid*100:.2f}%  gates={spn.get('gates')}  depth={spn.get('depth')}")
        print(f"  qiskit  direct : {qkd.get('job_id')}  fidelity={qk_fid*100:.2f}%  gates={qkd.get('gates')}  depth={qkd.get('depth')}")

    print()
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"  {'test':<12s}  {'qiskit fid':>11s}  {'spinor fid':>11s}  {'ratio':>7s}  {'ideal':>7s}")
    for r in results:
        c = r["case"]
        ratio = (r["spn_fid"] / r["qk_fid"]) if r["qk_fid"] > 0 else 0.0
        print(f"  {c['name']:<12s}  {r['qk_fid']*100:>10.2f}%  {r['spn_fid']*100:>10.2f}%  {ratio:>7.3f}  {r['ideal_fid']*100:>6.2f}%")

    return 0


if __name__ == "__main__":
    sys.exit(main())
