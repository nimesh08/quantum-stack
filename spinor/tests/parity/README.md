# Spinor ↔ Qiskit parity tests

Functional parity criterion (per the plan):
- Same gate set
- Gate count within ±5 %
- Depth within ±5 %
- Equivalent unitary up to a global phase (||U_spn − e^{iφ} U_qk|| < 1e-9)

## Run

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt

# Spinor side relies on `spinorc` on PATH (or pointed at by
# SPINOR_BIN). Registry root via SPINOR_REGISTRY_ROOT.
export SPINOR_BIN=/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/build/spinor/cli/spinorc
export SPINOR_REGISTRY_ROOT=/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack/spinor/registry

python3 run_parity.py
```

The script uses `FakeKingstonV2` (or a stand-in if Qiskit doesn't
ship one in this version) — no IBM credentials needed.
