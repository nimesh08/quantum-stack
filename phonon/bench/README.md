# Phase B benchmark harness

`bench.py` compares the gate counts and depth produced by:

- **us** — the Phonon optimizer + Phase A's `spinorc compile`.
- **pytket** — TKET 2.16.0 (Apache-2.0, Quantinuum/CQCL), used as
  the permanent benchmark target per Phonon Deep-Dive §8.
- **qiskit** — Qiskit's transpiler at `optimization_level=3`,
  used **only** as a comparison baseline. Rule 5 forbids
  Qiskit on our optimize path; this is benchmark plumbing only.

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install pytket==2.16.0 qiskit
```

## Run

Compile a Phonon source through the production CLI first, then
hand the OpenQASM 3 artifact to the bench:

```bash
$ ./build/spinor/cli/spinorc emit -t ibm_heron_r2 -f qasm3 \
    phonon/tests/corpus/bell_pair_func.phn  > out.qasm
$ python3 phonon/bench/bench.py out.qasm
```

Sample output:

```
| Pipeline | Gates | Depth |
| --- | --- | --- |
| us (phonon → spinor) | 17 | 17 |
| pytket TKET 2.16.0   | 22 | 19 |
| qiskit (baseline)    | 26 | 22 |
```

The harness is deliberately *not* part of the CI hot loop —
both pytket and qiskit are heavyweight Python deps that don't
belong in the C++-first CI. Run locally when investigating
optimizer quality regressions.

## Why pytket and Qiskit are NOT linked into our optimize path

- **pytket** ships a mature router (the SABRE-style algorithm
  the Phase A M5 paper cites). The Phonon Deep-Dive §8 names
  it explicitly as a temporary baseline you can wire behind
  the same routing interface and a permanent benchmark target
  to beat. Phase B treats it solely as the latter; Phase A's
  own SABRE is hardened and shipped (15 ctest entries / 116
  cases green).
- **Qiskit** is a vendor compiler with its own optimizer.
  Rule 5 forbids letting it touch the optimize path; the only
  Phase A appearance is the IBM submission shim (M10), which
  is in *verbatim mode* — Qiskit forwards the bytes we
  produce without re-optimization.
