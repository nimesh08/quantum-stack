# Cookbook — `calibration`

## 1. Run all chips once (cron-like)

```bash
calibration --once
```

Prints one JSON-line result per chip.

## 2. Run a daemon at 02:00 UTC

```bash
calibration                  # default schedule
```

## 3. Custom schedule

```bash
calibration --cron-hour 1 --cron-minute 30
```

## 4. Write your own provider

See [Add a calibration provider](../../tutorial/add_a_provider.md) for
the full walkthrough. In summary:

```python
# platform/calibration/src/calibration/providers/my_chip.py
from .base import CalibrationProvider

class MyChipProvider(CalibrationProvider):
    name = "my_chip"
    def fetch(self, chip_id):
        # ... call your API, return a dict with the right shape ...
        return {
            "chip_id": chip_id,
            "source": self.name,
            "single_qubit_errors": {...},
            "two_qubit_errors":   {...},
            "readout_errors":     {...},
            "t1_us":              {...},
            "t2_us":              {...},
        }
```

Register it:

```python
# platform/calibration/src/calibration/providers/__init__.py
from .my_chip import MyChipProvider
_REGISTRY["my_chip"] = MyChipProvider
```

Update the chip YAML:

```yaml
calibration:
  source: my_chip
  refresh: nightly
  store: ~/.cache/spinor/calibration/<chip>.json
```

## 5. Inspect the latest fetch

```bash
cat ~/.cache/spinor/calibration/ibm_heron_r2.json | jq
```

## See also

[Python autodoc](../../api/python/calibration/index.md), [Add a provider](../../tutorial/add_a_provider.md)
