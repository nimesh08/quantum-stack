# heisenberg-spinor-submit

Provider submission adapters for the Heisenberg Quantum Stack — three
provider adapters with a uniform interface, all submitting in
**verbatim / pass-through** mode (Rule 5):

- `ibm`   — Qiskit Runtime (`qiskit-ibm-runtime`)
- `aws`   — Braket SDK (`amazon-braket-sdk`)
- `azure` — Azure Quantum (`azure-quantum`)
- `local` — runs `spinorc` + the C++ statevector simulator
- `qci` / `anyon` / `tii` / `alicebob` — cassette-only today

```bash
pip install heisenberg-spinor-submit
```

The Python import name is still `spinor_submit`:

```python
from spinor_submit import submit
```

## Modes

Set `SPINOR_SUBMIT_MODE` to one of:

- `cassette` (default) — replay recorded histograms from
  `cassettes/<provider>/<program>.json`. Used in CI; no
  credentials required.
- `live` — talk to the real provider. Requires the SDK's standard
  credential mechanism (env vars / config file). Slow; runs only
  when explicitly requested.
- `local` — runs the C++ `spinorc` binary; useful for
  end-to-end smoke testing without provider tokens.

## Usage

```python
from spinor_submit import submit

with open("bell.qasm") as f:
    qasm = f.read()

# Cassette (default; no credentials):
hist = submit(qasm, chip="ibm_heron_r2",
              provider="ibm", shots=1000, program_name="bell")
print(hist.counts)

# Live (needs credentials in env):
import os
os.environ["SPINOR_SUBMIT_MODE"] = "live"
hist = submit(qasm, chip="ibm_heron_r2",
              provider="ibm", shots=1000)
```

## Recording new cassettes

```
SPINOR_SUBMIT_MODE=live python -m spinor_submit.record \
    --provider ibm --chip ibm_heron_r2 --program bell \
    --qasm bell.qasm --shots 1000
```

(Recorder helper not yet written; recording is a one-time manual
step done by maintainers when first integrating with a provider.)
