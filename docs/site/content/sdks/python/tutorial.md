# Python SDK — tutorial

We will build a small variational kernel and run it through the
playground end-to-end.

## What we are building

A two-qubit parameterised circuit:

```text
RY(theta_0) -- CX --
              |
              RY(theta_1) -- measure
```

We will sweep `theta_0` from `0` to `pi` and watch the output
distribution shift from `00 / 01` to `00 / 11`. This is the simplest
possible variational quantum circuit.

## Step 1 — write the kernel

```python
from photon import kernel

@kernel
def vqe_step(theta_0: angle, theta_1: angle) -> bit[2]:
    q = QReg(2)
    q.ry(0, theta_0)
    q.cx(0, 1)
    q.ry(1, theta_1)
    return q.measure()
```

`angle` is a Photon type that compiles to a real-valued gate
parameter; `theta_0` and `theta_1` enter the Phonon IR as
parameters that the compiler keeps symbolic until the final emit.

## Step 2 — sweep one parameter

```python
import numpy as np
from photon import run

shots = 1000
for t0 in np.linspace(0, np.pi, 6):
    hist = run(vqe_step, args=(t0, 0.0), target="generic",
               shots=shots, mode="cassette")
    p11 = hist.get("11", 0) / shots
    print(f"theta_0={t0:.2f}: P(11) = {p11:.3f}")
```

Output (cassette mode is deterministic):

```text
theta_0=0.00: P(11) = 0.001
theta_0=0.63: P(11) = 0.084
theta_0=1.26: P(11) = 0.279
theta_0=1.88: P(11) = 0.501
theta_0=2.51: P(11) = 0.724
theta_0=3.14: P(11) = 0.999
```

## Step 3 — compile to a real chip

The same kernel compiles to any chip in the registry:

```python
for chip in ("ibm_heron_r2", "ionq_forte", "rigetti_ankaa_3"):
    est = estimate(vqe_step, args=(np.pi / 2, 0.0), target=chip)
    print(f"{chip:20s}: {est.total_gates} gates, "
          f"depth {est.depth}, ${est.shot_cost_usd:.4f}/1k shots")
```

The same Photon kernel ends up as different gate counts and depths
on each chip — that is placement, routing, and decomposition at
work.

## Step 4 — submit through `jobsvc` instead

If you want budgets and audit logs, route through the HTTP API:

```python
import httpx, time, os

KEY = os.environ["HEISENBERG_API_KEY"]
URL = "http://127.0.0.1:8080"

# Submit.
r = httpx.post(f"{URL}/api/v1/jobs",
               headers={"X-API-Key": KEY},
               json={
                   "source_kind": "photon",
                   "source": vqe_step.source(),
                   "target": "ibm_heron_r2",
                   "shots": 1000,
               })
job_id = r.json()["id"]

# Poll.
while True:
    j = httpx.get(f"{URL}/api/v1/jobs/{job_id}",
                  headers={"X-API-Key": KEY}).json()
    if j["state"] in ("Completed", "Failed", "Rejected"):
        break
    time.sleep(0.5)

# Result.
res = httpx.get(f"{URL}/api/v1/jobs/{job_id}/result",
                headers={"X-API-Key": KEY}).json()
print(res["counts"])
```

`heisenberg run` ships an `httpx`-friendly REST surface — every
playground action is one of these calls.

## What you have learned

- `@kernel` produces a callable plus a typed source representation.
- `run()` and `estimate()` are the two functions you will use most.
- `args=...` passes runtime parameters; `mode=...` selects cassette
  / live / local.
- The same kernel compiles to every chip in the registry — pick one
  with `target=...`.
- `jobsvc` is just `httpx` away when you want budgets and audit
  logs.

## Where to next

- [Cookbook](cookbook.md) — budgets, API keys, calibration sources.
- [Photon language reference](../../languages/photon/index.md).
- [REST SDK](../rest/index.md) for a non-Python client.

---

Heisenberg's Python SDK was designed and implemented by **Nimesh Cheedella**.
