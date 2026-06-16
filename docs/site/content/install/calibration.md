# `calibration`

The Phase D nightly calibration refresh service. APScheduler-driven;
fetches each chip's calibration JSON and atomically replaces
`~/.cache/spinor/calibration/<chip>.json` (the path the chip YAMLs
declare). The compiler reads that file at compile time.

## Install (developer mode)

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/calibration
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install -e ".[test]"
```

## Smoke test (one-shot mode)

```bash
calibration --once
```

Output:

```
{"chip": "ibm_heron_r2",      "ok": true, "skipped": false, ...}
{"chip": "ionq_aria_proto",   "ok": true, "skipped": false, ...}
{"chip": "ionq_forte",        "ok": true, "skipped": false, ...}
{"chip": "quantinuum_helios", "ok": true, "skipped": false, ...}
```

`--once` runs every chip whose YAML says `calibration.refresh: nightly`
and exits.

## Run as a daemon

```bash
calibration
# scheduler.start version=0.4.0+phased.m5 hour=2 minute=0
```

By default fires at 02:00 UTC every day. Override:

```bash
calibration --cron-hour 1 --cron-minute 30
```

## Run tests

```bash
pytest tests/             # 19 tests
```

Expected: **19/19 green**.

## Provider matrix

| Source (chip YAML) | Implementation | At v1 |
|---|---|---|
| `fixture` | deterministic synthetic data | always works |
| `ibm_runtime_api` | `qiskit-ibm-runtime` `backend.properties()` | live (needs IBM token) |
| `aws` | TODO | stub — keeps previous file (D5) |
| `azure` | TODO | stub — keeps previous file (D5) |

Bring up your own:
[Add a calibration provider](../tutorial/add_a_provider.md).

## See also

- [`calibration` autodoc](../api/python/calibration/index.md)
- [Calibration architecture](../guide/operations.md#calibration)
