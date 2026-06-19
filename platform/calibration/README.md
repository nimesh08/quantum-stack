# calibration

Nightly calibration refresh service for the Heisenberg Quantum
Stack. Reads each chip YAML in `spinor/registry/chips/`, fetches
fresh calibration data from the chip's provider plugin, and
atomically writes the per-chip JSON the compiler reads on every
placement decision.

```bash
pip install calibration
calibration --once    # run all chips once and exit (cron-like)
calibration           # background scheduler (default 02:00 UTC)
```

Author: **Nimesh Cheedella**.
