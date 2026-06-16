# Python — `calibration`

The nightly calibration refresh service. Four modules, all
auto-generated from docstrings.

## Modules

<div class="grid cards" markdown>

-   :material-cloud-download:{ .lg .middle } **[`calibration.providers`](providers.md)**

    ---

    Abstract `CalibrationProvider` plus the four concrete shipped
    implementations: `fixture`, `ibm`, `aws` (stub), `azure` (stub).

-   :material-content-save:{ .lg .middle } **[`calibration.writer`](writer.md)**

    ---

    [`write_atomic`][calibration.writer.write_atomic] —
    `tmpfile + os.replace` so an interrupted write never leaves a
    half-file.

-   :material-compare:{ .lg .middle } **[`calibration.diff`](diff.md)**

    ---

    Drift detector. Flags qubits and pairs whose error rates
    changed by more than the configured relative threshold.

-   :material-clock:{ .lg .middle } **[`calibration.main`](main.md)**

    ---

    APScheduler entry point.
    [`run_once`][calibration.main.run_once] is the cron-replacement
    mode; [`run`][calibration.main.run] is the daemon.

</div>
