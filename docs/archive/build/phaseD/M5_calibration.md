# M5 — Calibration refresh (seam 2)

End: a scheduled service writes per-chip calibration JSON to
`~/.cache/spinor/calibration/<chip>.json` (the path the chip YAMLs
already declare), so the compiler's placement pass picks up fresh
data on the next compile.

## Components

- `calibration.providers.{ibm,aws,azure,fixture}` — each implements
  `fetch(chip_id) -> dict`. AWS and Azure are v1 stubs (D5).
- `calibration.writer.write_atomic(path, body)` — `tmp + os.replace`.
- `calibration.diff.diff(old, new)` — flags qubits and pairs whose
  error rate changed by >50% (configurable).
- `calibration.main.refresh_one(plan)` — load → fetch → write →
  diff. On fetch failure, the previous file is preserved untouched.
- `calibration.main.run` — APScheduler `BlockingScheduler` with a
  daily `CronTrigger(hour=2, minute=0, timezone=UTC)`. `--once` mode
  drives all chips once and exits (cron-replacement).

## Tests landed

- `tests/unit/test_writer.py` — atomic write semantics, idempotency.
- `tests/unit/test_providers.py` — provider registry, stub raises.
- `tests/unit/test_scheduler.py` — cron fires at 02:00 UTC.
- `tests/integration/test_refresh.py` — load_plans, run_once writes
  only nightly chips, provider failure keeps previous file, drift
  reported when error rates shift dramatically.

19/19 calibration tests green; 94/94 cumulative across jobsvc +
calibration at end of M5.
