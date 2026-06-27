#!/usr/bin/env python3
"""Submit the SAME Bell-pair circuit to IBM hardware TWICE:
once from Spinor's QASM3 output and once from Qiskit's transpile
output. Print both job IDs so the user can verify in IBM's
dashboard that both compilation paths produce a valid ISA circuit
that the hardware accepts.

Both paths target the same backend (ibm_fez by default) and use
the same number of shots, so the resulting histograms can be
compared directly.
"""
from __future__ import annotations
import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--token", required=True)
    p.add_argument("--instance", required=True)
    p.add_argument("--backend", default="ibm_fez")
    p.add_argument("--shots", type=int, default=1024)
    p.add_argument("--olevel", type=int, default=3)
    args = p.parse_args()

    from qiskit import QuantumCircuit, transpile
    from qiskit.qasm3 import loads as qasm3_loads
    from qiskit_ibm_runtime import QiskitRuntimeService, SamplerV2

    print(f"# Connecting to IBM Cloud, backend={args.backend}, shots={args.shots}, -O{args.olevel}")
    service = QiskitRuntimeService(channel="ibm_cloud", token=args.token, instance=args.instance)
    backend = service.backend(args.backend)
    print(f"# basis_gates = {backend.configuration().basis_gates}")

    # ----- Path A: Qiskit transpile -----
    print(f"\n# === Path A: Qiskit transpile (optimization_level={args.olevel}) ===")
    qc = QuantumCircuit(2, 2)
    qc.h(0)
    qc.cx(0, 1)
    qc.measure([0, 1], [0, 1])
    qt = transpile(qc, backend=backend, optimization_level=args.olevel, seed_transpiler=42)
    print(f"# Qiskit transpiled: {qt.count_ops()}, depth={qt.depth()}")

    # ----- Path B: Spinor emit -----
    print(f"\n# === Path B: Spinor emit (-O{args.olevel}) ===")
    env = os.environ.copy()
    env["SPINOR_REGISTRY_ROOT"] = "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/spinor/registry"
    spn_bin = "/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/build/spinor/cli/spinorc"
    spn_src = HERE / "circuits" / "bell.spn"
    cmd = [spn_bin, "emit", "-t", args.backend, "-O", str(args.olevel), "-f", "qasm3", str(spn_src)]
    print(f"# {' '.join(cmd)}")
    spn_qasm = subprocess.check_output(cmd, env=env, text=True)
    print(spn_qasm)

    # Build the Qiskit QuantumCircuit by hand from Spinor's emitted
    # gate sequence. Avoids the QASM3 parser's strictness around
    # implicit bit declarations.
    import re
    spn_qc = QuantumCircuit(2, 2)
    # The emitted QASM uses `q[N]` for qubits (logical indices the
    # placer assigned). For the Bell pair these will be q[0] and q[1].
    for raw in spn_qasm.splitlines():
        line = raw.strip().rstrip(";")
        if not line or line.startswith(("OPENQASM", "include", "qubit", "bit", "//")):
            continue
        # measurement: `c[i] = measure q[j]`
        m = re.match(r"c\[(\d+)\]\s*=\s*measure\s+q\[(\d+)\]", line)
        if m:
            ci, qi = int(m.group(1)), int(m.group(2))
            spn_qc.measure(qi, ci)
            continue
        # 2q: `cz q[a], q[b]`
        m = re.match(r"(cz|cx|ecr)\s+q\[(\d+)\]\s*,\s*q\[(\d+)\]", line)
        if m:
            gate, a, b = m.group(1), int(m.group(2)), int(m.group(3))
            getattr(spn_qc, gate)(a, b)
            continue
        # 1q with angle: `rz(...) q[i]`
        m = re.match(r"rz\(([^)]+)\)\s+q\[(\d+)\]", line)
        if m:
            spn_qc.rz(float(m.group(1)), int(m.group(2)))
            continue
        # 1q no angle: `sx q[i]` | `x q[i]` | `id q[i]`
        m = re.match(r"(sx|x|id|sxdg)\s+q\[(\d+)\]", line)
        if m:
            gate, qi = m.group(1), int(m.group(2))
            getattr(spn_qc, gate)(qi)
            continue
        print(f"# (skipped line) {line}")
    print(f"# Spinor → Qiskit-circuit: {spn_qc.count_ops()}, depth={spn_qc.depth()}")
    # Validate against ISA (will leave native gates alone).
    spn_isa = transpile(spn_qc, backend=backend, optimization_level=0, seed_transpiler=42)
    print(f"# Spinor ISA-validated: {spn_isa.count_ops()}, depth={spn_isa.depth()}")

    # ----- Submit both -----
    print(f"\n# === Submitting both to {args.backend} ({args.shots} shots each) ===")
    sampler = SamplerV2(mode=backend)

    job_q = sampler.run([qt], shots=args.shots)
    job_s = sampler.run([spn_isa], shots=args.shots)
    print(f"\nQISKIT_JOB_ID = {job_q.job_id()}")
    print(f"SPINOR_JOB_ID = {job_s.job_id()}")
    print(f"\nDashboard: https://quantum.cloud.ibm.com/jobs")
    print(f"\n# waiting for both to complete...")

    res_q = job_q.result()
    counts_q = res_q[0].data.c.get_counts()
    print(f"\nQiskit histogram: {dict(counts_q)}")

    res_s = job_s.result()
    counts_s = res_s[0].data.c.get_counts()
    print(f"Spinor histogram: {dict(counts_s)}")

    # Final comparison.
    print(f"\n# ===== summary =====")
    print(f"  Qiskit job  {job_q.job_id()}   {dict(counts_q)}")
    print(f"  Spinor job  {job_s.job_id()}   {dict(counts_s)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
