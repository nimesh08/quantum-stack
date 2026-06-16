# Operations

Running the platform in production. Every section is a runbook —
what to look for, what to change, what to do when something breaks.

## Containers and processes

`platform/deploy/docker-compose.yml` ships five services:

| Service | Image | Replicas | Purpose |
|---|---|---|---|
| `db` | `postgres:17.10-alpine` | 1 | jobs / results / audit log / calibration snapshots |
| `jobsvc` | `Dockerfile.jobsvc` | 1 | FastAPI on `:8000`; `alembic upgrade head` on start |
| `worker` | `Dockerfile.worker` | 2 | claims jobs, drives `spinor_submit`, stores histograms |
| `scheduler` | `Dockerfile.calibration` | 1 | APScheduler nightly refresh |
| `playground` | `Dockerfile.playground` | 1 | nginx serving the React build, proxies `/api` to `jobsvc` |

Bring up: `cd platform/deploy && ./run.sh up -d`. Smoke: `./run.sh smoke`.

## Auth

Two equivalent paths (see [Decisions D2](decisions.md)):

- **JWT** (HS256, 60-min access, 14-day refresh) — Playground.
- **API key** (`prefix.body`, bcrypt-hashed, identified by 8-char prefix) —
  programmatic.

Seed an admin:

```bash
docker compose exec jobsvc python -m jobsvc.seed admin@local pwd admin
```

Rotate the JWT secret in production by setting `JWT_SECRET` in
`.env` and restarting `jobsvc`.

## Live providers

Default `SPINOR_SUBMIT_MODE=cassette` replays recorded JSON; safe in
CI. To switch to real provider calls:

=== "IBM Quantum"

    ```bash
    SPINOR_SUBMIT_MODE=live
    IBM_QUANTUM_TOKEN=...
    ```

    The Phase A adapter at
    [`spinor_submit._live_ibm`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/submit/python/spinor_submit/__init__.py)
    uses `qiskit-ibm-runtime` with `SamplerV2(skip_transpilation=True)` —
    verbatim, by Rule 5.

=== "AWS Braket"

    Standard `~/.aws/credentials`. The adapter maps chip ids to ARNs
    in `_live_aws`; extend that map for new ARNs. Submission goes via
    `device.run(Program(source=qasm_text), shots=...)` — verbatim.

=== "Azure Quantum"

    ```bash
    AZURE_QUANTUM_RESOURCE_ID=/subscriptions/.../workspaces/...
    AZURE_QUANTUM_LOCATION=westus
    ```

    `target.submit(input_data=qasm, input_data_format="openqasm-v3")`.

## Observability

### Logs

`structlog` JSON. Every line carries `request_id`; `request.start` and
`request.end` bracket each HTTP call:

```json
{"event":"request.end","method":"POST","path":"/api/v1/jobs",
 "status":201,"duration_ms":18,"request_id":"7c1a5...",
 "level":"info","timestamp":"2026-06-16T..."}
```

Send `x-request-id: <id>` to correlate client-side traces; the
service echoes it back in the response header.

### Metrics

Prometheus text at `GET /metrics`:

| Metric | Type | Labels |
|---|---|---|
| `jobs_total` | counter | `state` ∈ {Completed, Rejected, Failed} |
| `job_duration_seconds` | histogram | `state` |
| `queue_depth` | gauge | — |
| `worker_lease_expirations_total` | counter | — |
| `provider_latency_seconds` | histogram | `provider` |
| `errors_total` | counter | `kind` ∈ {our, provider} |
| `calibration_refresh_total` | counter | `chip`, `ok` |

### Health

- `GET /healthz` — process up.
- `GET /readyz` — process up *and* database reachable.

Wire your container orchestrator's liveness probe to `/healthz` and
readiness to `/readyz`.

## Calibration

Scheduler runs daily at 02:00 UTC. Cron expression lives in
[`calibration.main.run`](../api/python/calibration/main.md#calibration.main.run).

### Force a refresh

```bash
docker compose exec scheduler calibration --once
```

### Per-chip schedule

Each chip YAML declares `calibration.refresh: nightly | never` and
`calibration.source: ibm_runtime_api | aws | azure | fixture | <custom>`.
At v1, `aws` and `azure` are stubs (Decisions D5); they keep the
previous file untouched. Add a real provider with
[Add a calibration provider](../tutorial/add_a_provider.md).

### Atomic write semantics

`writer.write_atomic` writes to `<path>.tmp` then `os.replace`. If the
fetch raises mid-write, the previous file stays intact; the
`calibration_snapshots` table records `ok=false` with the error.

## Scaling

The default compose runs **2 workers**. To scale:

```bash
docker compose --project-directory ../.. -f docker-compose.yml \
    up -d --scale worker=8
```

The Postgres queue (`FOR UPDATE SKIP LOCKED`) handles N workers
without coordination. Watch `queue_depth` on `/metrics`; if it
climbs steadily, add workers (or look at why providers are slow —
`provider_latency_seconds` will tell you).

## Database migrations

Alembic on first start of `jobsvc`:

```bash
docker compose exec jobsvc alembic current
docker compose exec jobsvc alembic upgrade head
docker compose exec jobsvc alembic history
```

For a clean reset (destroys data):

```bash
./run.sh down            # docker compose down -v
./run.sh up -d
```

## Backups

Everything live is in Postgres. Snapshot:

```bash
docker compose exec db pg_dump -U jobsvc jobsvc | gzip > backup-$(date +%F).sql.gz
```

Restore:

```bash
gunzip -c backup-2026-06-16.sql.gz | \
    docker compose exec -T db psql -U jobsvc -d jobsvc
```

## Troubleshooting

| Symptom | Likely cause | Remedy |
|---|---|---|
| `502 Bad Gateway` on Playground | `jobsvc` still starting | `docker compose logs jobsvc`; alembic upgrade can take ~30s on first run |
| Login banner says "invalid credentials" | seeded user not yet present | `docker compose exec jobsvc python -m jobsvc.seed admin@local admin-password admin` |
| Histogram never appears | no worker running | `docker compose ps worker`; `docker compose up -d worker` |
| `errors_total{kind="provider"}` spiking | provider outage / rate limit | switch back to cassette mode or wait |
| `queue_depth` rising | not enough workers | `docker compose up -d --scale worker=N` |
| Calibration never refreshes | wrong cron / scheduler down | `docker compose exec scheduler calibration --once` to test |
| 402 on a job you expect to fit | recent spend window has more in it | `GET /api/v1/me/budget` and `GET /api/v1/jobs?state=Completed` |
