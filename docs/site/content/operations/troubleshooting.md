# Operations — Troubleshooting

Common problems and the fix.

## `heisenberg init` fails with `could not locate jobsvc alembic.ini`

You upgraded `heisenberg` but did not upgrade `jobsvc`, or the two
came from different versions. Reinstall both:

```bash
pip install --upgrade --force-reinstall heisenberg
```

If that does not fix it, run with `--verbose`:

```bash
HEISENBERG_DATA_DIR=/tmp/h python -X dev -m heisenberg init
```

The traceback will name the file path it could not find.

## `heisenberg run` exits immediately with `address already in use`

Another `heisenberg run` is still bound to port 8080. Either:

```bash
heisenberg stop
```

Or pick a different port:

```bash
heisenberg run --port 9090
```

## `/api/v1/targets` returns an empty list

The launcher could not find the chip registry. Check:

```bash
heisenberg version | grep registry
```

If it says `(not found)`, set `SPINOR_REGISTRY_ROOT`:

```bash
SPINOR_REGISTRY_ROOT=/path/to/spinor/registry heisenberg run
```

Or copy the registry into one of the search paths
(`/usr/share/heisenberg/registry` for system installs).

## `compile failed: emitCX: no recipe for entangler 'cz'`

The chip is CZ-native but the C++ KAK decomposer does not yet ship
the `cxFromCz` recipe. This affects 12 chips and is tracked in the
[unsupported-chips ledger](../internals/chips_unsupported.md). One
PR to `spinor/passes/Decomposition.cpp` unblocks all 12 at once.

Workaround: pick a different target (any IBM Heron r2 / Eagle / IonQ
/ Quantinuum chip uses a recipe that is already shipped).

## `RuntimeError: live submission to 'qci' is not wired yet`

You set `SPINOR_SUBMIT_MODE=live` but the chosen provider does not
have a public production REST URL yet. Either:

- Use `SPINOR_SUBMIT_MODE=cassette` for tests.
- Pick a provider with a public endpoint (`ibm`, `aws`, `azure`).
- Wait for the vendor to publish their endpoint (tracked in
  [unsupported chips](../internals/chips_unsupported.md)).

## `heisenberg run --production` does not bind to `0.0.0.0`

`--production` *does* bind `0.0.0.0`, but most cloud firewalls
block 8080 by default. Either open the port or front the launcher
with a reverse proxy that forwards `:443 -> 127.0.0.1:8080` (see
[Operations / Install / Reverse proxy](install.md#step-8-reverse-proxy-optional)).

## SQLite locking errors under load

```
(sqlite3.OperationalError) database is locked
```

SQLite serialises writes. Above ~50 jobs/sec or with > 1 worker
process you will see this. Switch to Postgres
([Operations / Postgres](postgres.md)).

## The browser does not auto-open

`heisenberg run` calls `webbrowser.open()`, which on headless
servers (no `DISPLAY`) silently does nothing. That is fine;
visit the URL printed in the terminal. To suppress the attempt:

```bash
heisenberg run --no-browser
```

## Worker stops processing jobs

Two likely causes:

1. The lease lapsed (default 5 minutes). Look for
   `worker.lease_expired` events. The job is automatically
   reclaimed; no action needed.
2. The worker crashed. Check `journalctl -u heisenberg-worker -n 100`.
   If you see a Python traceback, the most likely culprit is a
   provider SDK panic — file an issue.

## Calibration job keeps failing for one chip

```bash
sudo journalctl -u heisenberg-calibration | grep refresh.failed
```

The error string in the log message names the upstream cause. The
previous calibration JSON is left intact (atomic-rename guarantee),
so jobs continue to compile against the last good fetch.

## Where to ask for help

- Open an issue on
  [GitHub](https://github.com/nimesh08/quantum-stack/issues) with
  the `request_id` of a failing request and the relevant log
  lines.
- For security issues see [`SECURITY.md`](https://github.com/nimesh08/quantum-stack/blob/main/SECURITY.md).

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
