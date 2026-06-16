# GHZ on a real chip

Building on the [Bell tutorial](bell.md), let's submit a 3-qubit GHZ
state to `ibm_heron_r2` and watch the cost-control seam in action.

## What we're building

The GHZ state is the n-qubit generalisation of Bell:
`(|000…0⟩ + |111…1⟩) / √2`. For 3 qubits, only `000` and `111` ever
appear at measurement.

```
target generic
qubit q[3]
bit c[3]
h q[0]
cx q[0], q[1]
cx q[0], q[2]
c = measure q
```

## 1. Pick a real chip

`ibm_heron_r2` (Heron r2, 156 qubits, $0.00033/shot, heavy-hex
lattice). The compiler will:

- **Place** the three logical qubits on physical qubits selected from
  last night's calibration.
- **Route** them onto neighboring positions (heavy-hex isn't
  all-to-all).
- **Decompose** `h` into `rz/sx` and `cx` into the native `ecr`
  gate.

The submission is **verbatim** — IBM's transpiler isn't allowed to
rewrite it. That's how we own the result quality.

## 2. Submit it

```bash
TOKEN=$(curl -s -X POST http://localhost:8000/api/v1/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"admin@local","password":"admin-password"}' | jq -r .access_token)

curl -s -X POST http://localhost:8000/api/v1/jobs \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
        "source": "target generic\nqubit q[3]\nh q[0]\ncx q[0], q[1]\ncx q[0], q[2]\n",
        "source_kind": "spinor",
        "target": "ibm_heron_r2",
        "shots": 1000,
        "name": "ghz3"
      }' | jq
```

Response (cassette mode):

```json
{
  "id": "...",
  "name": "ghz3",
  "target": "ibm_heron_r2",
  "shots": 1000,
  "state": "Queued",
  "estimate": {
    "num_qubits": 3,
    "depth": 8,
    "two_qubit_count": 2,
    "t_count": 0
  },
  "dollar_cost": "0.330000",
  "provider": "ibm"
}
```

`dollar_cost = shots × pricing.per_shot_usd = 1000 × 0.00033 = $0.33`.
That's the cost-control input.

## 3. Watch over-budget rejection

Tighten the daily budget below the cost:

```bash
curl -s -X PATCH -H "Authorization: Bearer $TOKEN" \
     -H 'Content-Type: application/json' \
     -d '{"daily_usd":"0.10"}' \
     http://localhost:8000/api/v1/me/budget

curl -s -X POST -H "Authorization: Bearer $TOKEN" \
     -H 'Content-Type: application/json' \
     -d '{
           "source": "target generic\nqubit q[3]\nh q[0]\ncx q[0], q[1]\ncx q[0], q[2]\n",
           "source_kind": "spinor",
           "target": "ibm_heron_r2",
           "shots": 1000
         }' \
     http://localhost:8000/api/v1/jobs
```

Now the response is **402 Payment Required**, before any provider
network call:

```json
{
  "detail": {
    "reason": "exceeds_daily_budget",
    "dollar_cost": "0.330000",
    "daily_usd": "0.10",
    "recent_daily_spend": "0.330000",
    "daily_remaining": "-0.230000",
    "suggestion": "reduce shots, pick a cheaper target, or raise the daily budget."
  }
}
```

Reset the budget if you want to keep going:

```bash
curl -s -X PATCH -H "Authorization: Bearer $TOKEN" \
     -H 'Content-Type: application/json' \
     -d '{"daily_usd":"100.00"}' \
     http://localhost:8000/api/v1/me/budget
```

## 4. Inspect what was actually submitted

The compiled artifact lives in the worker. The Phase A cassette at
[`spinor/submit/python/spinor_submit/cassettes/ibm/bell.json`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/submit/python/spinor_submit/cassettes/ibm/bell.json)
shows the recorded response. For live mode, the worker submits raw
OpenQASM 3 with `skip_transpilation=True` — see
[`spinor_submit._live_ibm`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/submit/python/spinor_submit/__init__.py).

## 5. See it in metrics

```bash
curl -s http://localhost:8000/metrics | grep -E '^(jobs_total|errors_total|provider_latency)'
```

Output looks like:

```
jobs_total{state="Completed"} 1.0
jobs_total{state="Rejected"} 1.0
jobs_total{state="Failed"} 0.0
errors_total{kind="our"} 0.0
errors_total{kind="provider"} 0.0
provider_latency_seconds_count{provider="ibm"} 1.0
provider_latency_seconds_sum{provider="ibm"} 0.012
```

The `errors_total{kind="provider"}` vs `errors_total{kind="our"}` split
is the workhorse for incident response: provider outages spike the
right side and leave the left flat.

## Where to next

- [Add a chip in 30 minutes](add_a_chip.md) — supporting a new
  device without touching compiler code.
- [Operations](../guide/operations.md) — turning on live providers,
  tuning the calibration scheduler, configuring observability.
- [`jobsvc.cost`](../api/python/jobsvc/cost.md) — the cost-control
  function in detail.
