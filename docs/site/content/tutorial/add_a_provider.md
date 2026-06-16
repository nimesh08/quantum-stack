# Add a calibration provider

The nightly calibration refresh fetches per-chip data and writes it to
`~/.cache/spinor/calibration/<chip>.json`. Out of the box we ship four
provider implementations (`fixture`, `ibm`, `aws` stub, `azure` stub).
This tutorial adds a fifth, fictional provider for our `acme_q1` chip
from the [previous tutorial](add_a_chip.md).

## 1. Subclass `CalibrationProvider`

Create
[`platform/calibration/src/calibration/providers/acme.py`](https://github.com/nimesh08/quantum-stack/blob/main/platform/calibration/src/calibration):

```python
"""Acme calibration provider — toy example."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

import httpx

from .base import CalibrationProvider


class AcmeProvider(CalibrationProvider):
    """Fetches calibration JSON from a hypothetical Acme HTTP API."""

    name = "acme"

    def __init__(self, base_url: str = "https://acme.example/cal/v1"):
        self.base_url = base_url

    def fetch(self, chip_id: str) -> dict[str, Any]:
        with httpx.Client(timeout=30.0) as c:
            r = c.get(f"{self.base_url}/chips/{chip_id}")
            r.raise_for_status()
            data = r.json()
        return {
            "chip_id": chip_id,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "source": self.name,
            "single_qubit_errors": data["sq_errors"],
            "two_qubit_errors": data["tq_errors"],
            "readout_errors": data["readout"],
            "t1_us": data["t1"],
            "t2_us": data["t2"],
        }
```

## 2. Register it

Add it to [`providers/__init__.py`](https://github.com/nimesh08/quantum-stack/blob/main/platform/calibration/src/calibration/providers/__init__.py)'s
registry:

```python
from .acme import AcmeProvider

_REGISTRY: dict[str, type[CalibrationProvider]] = {
    "fixture": FixtureProvider,
    "ibm":     IbmProvider,
    "aws":     AwsProvider,
    "azure":   AzureProvider,
    "acme":    AcmeProvider,        # <- new
}
```

…and update [`main._resolve_provider`](https://github.com/nimesh08/quantum-stack/blob/main/platform/calibration/src/calibration/main.py)
so chip YAMLs that say `calibration.source: acme` route here:

```python
def _resolve_provider(name: str) -> str:
    if name in ("ibm_runtime_api", "ibm"):
        return "ibm"
    if name == "aws":   return "aws"
    if name == "azure": return "azure"
    if name == "acme":  return "acme"
    return "fixture"
```

## 3. Update the chip YAML

In [`spinor/registry/chips/acme_q1.yaml`](add_a_chip.md):

```yaml
calibration:
  source: acme           # <- was: fixture
  refresh: nightly       # <- was: never
  store: ~/.cache/spinor/calibration/acme_q1.json
```

## 4. Run a one-shot refresh

```bash
docker compose exec scheduler calibration --once
```

Output:

```json
{"chip": "acme_q1", "ok": true, "sha": "9c2f...", "drifted": false,
 "drifted_qubits": [], "drifted_pairs": []}
```

A new file appears at `~/.cache/spinor/calibration/acme_q1.json`. The
[`calibration_snapshots`](../api/python/jobsvc/models.md#jobsvc.models.CalibrationSnapshot)
table records the fetch.

## 5. Add a test

```python
# platform/calibration/tests/unit/test_acme.py
import respx
from calibration.providers.acme import AcmeProvider

@respx.mock
def test_acme_fetch_round_trips():
    respx.get("https://acme.example/cal/v1/chips/acme_q1").respond(json={
        "sq_errors": {"0": 0.001},
        "tq_errors": {"0-1": 0.01},
        "readout": {"0": 0.02},
        "t1": {"0": 100.0},
        "t2": {"0": 80.0},
    })
    body = AcmeProvider().fetch("acme_q1")
    assert body["chip_id"] == "acme_q1"
    assert body["single_qubit_errors"]["0"] == 0.001
```

```bash
cd platform/calibration && pytest tests/unit/test_acme.py -q
```

## How the writer protects you

[`calibration.writer.write_atomic`](../api/python/calibration/writer.md#calibration.writer.write_atomic)
writes to a tmpfile then `os.replace`. If the provider call raises
mid-fetch, the previous file is intact — see
[`refresh_one`](../api/python/calibration/main.md#calibration.main.refresh_one).
If the new payload's error rates jump >50% from the previous file,
[`diff.diff`](../api/python/calibration/diff.md#calibration.diff.diff)
flags the affected qubits/pairs and the
`calibration_refresh_total{ok="true"}` Prometheus counter ticks; the
log line carries `drift=true`.

## Where to next

- [Operations: calibration scheduling](../guide/operations.md#calibration) —
  cron tuning, multi-replica considerations.
- [Python: `calibration.providers`](../api/python/calibration/providers.md) —
  the full base class API.
