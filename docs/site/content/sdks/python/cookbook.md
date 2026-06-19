# Python SDK — cookbook

Copy-paste recipes for real Python tasks against Heisenberg.

## Set a daily / monthly budget

```python
import httpx, os

httpx.put(
    "http://127.0.0.1:8080/api/v1/users/me/budget",
    headers={"X-API-Key": os.environ["HEISENBERG_API_KEY"]},
    json={"daily_usd": "5.00", "monthly_usd": "50.00",
          "max_shots_per_job": 5000},
).raise_for_status()
```

The next time `cost.check_budget()` runs (on every job submit), it
will reject jobs that would push you over.

## Mint a fresh API key

```python
import httpx, os

r = httpx.post(
    "http://127.0.0.1:8080/api/v1/users/me/api-keys",
    headers={"X-API-Key": os.environ["HEISENBERG_API_KEY"]},
    json={"label": "ci-runner"},
)
print(r.json()["plaintext"])     # shown ONCE; copy now
```

The plaintext key has format `Q4r2p8aA.<32-char body>`. Only the
prefix and a bcrypt hash are stored server-side.

## Poll a job to completion

```python
import httpx, time, os

URL  = "http://127.0.0.1:8080"
HEAD = {"X-API-Key": os.environ["HEISENBERG_API_KEY"]}

def wait(job_id: str) -> dict:
    while True:
        r = httpx.get(f"{URL}/api/v1/jobs/{job_id}", headers=HEAD).json()
        if r["state"] in ("Completed", "Failed", "Rejected"):
            return r
        time.sleep(0.5)
```

For long-running jobs, prefer `httpx.AsyncClient` plus
`asyncio.wait_for(..., timeout=300)`.

## Record a cassette for a new program

```python
import os
os.environ["SPINOR_SUBMIT_MODE"] = "live"
os.environ["IBM_QUANTUM_TOKEN"] = "..."

from spinor_submit import submit, RECORD_DIR
hist = submit(qasm_text, chip="ibm_heron_r2", provider="ibm",
              shots=1000, program_name="my_circuit",
              record_to=RECORD_DIR)
```

`record_to` writes the histogram to `cassettes/ibm/my_circuit.json`
so subsequent CI runs replay deterministically.

## Use a chip with a custom calibration source

Edit the chip YAML's `calibration.source` field; the launcher's
nightly scheduler will pick up the new provider on the next run.
For one-off use:

```python
import json, pathlib
calib = pathlib.Path.home() / ".local/share/heisenberg/calibration"
calib.mkdir(parents=True, exist_ok=True)
(calib / "ibm_heron_r2.json").write_text(json.dumps({
    "qubits": [...],
    "edges":  [...],
}))
```

The compiler's placement pass picks up the file on the next compile.

## Switch all of jobsvc to a thread-pool worker

By default `heisenberg run` runs the worker on an asyncio task. For
CPU-heavy compilation, run a separate worker process:

```bash
heisenberg run --no-worker &        # API + scheduler only
JOBSVC_DATABASE_URL=$(heisenberg config get database_url) \
  python -m jobsvc.worker
```

Or use the systemd model: see
[Operations / Native systemd](../../operations/native_systemd.md).

## Use Heisenberg from Jupyter

```python
%pip install heisenberg
from photon import kernel, run, estimate
```

The `@kernel` decorator works inside Jupyter cells. `run(...,
mode="cassette")` is the default, so your notebooks do not need
cloud credentials to reproduce.

## Sniff what the compiler emits

```python
from photon import compile_to_qasm

print(compile_to_qasm(bell, target="ibm_heron_r2",
                      verbatim=True))
```

You get the OpenQASM 3.1 wrapped in `#pragma braket verbatim`. That
is exactly what the IBM SDK gets — RULE 5.

---

Heisenberg's Python SDK was designed and implemented by **Nimesh Cheedella**.
