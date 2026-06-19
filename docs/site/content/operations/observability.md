# Operations — Observability

Heisenberg ships three observability surfaces:

| Surface | Endpoint / output | Format |
|---------|-------------------|--------|
| Logs | stderr / journald | JSON when `JOBSVC_LOG_JSON=true` |
| Metrics | `/metrics` | Prometheus text exposition |
| Traces | (planned) | OpenTelemetry OTLP |

## Logs

Every request gets:

- A `request_id` (echoed in the `x-request-id` response header).
- A `request.start` and `request.end` event.
- Method, path, status, and duration in milliseconds.

In production (`heisenberg run --production` or systemd):

```json
{"event":"request.end","method":"POST","path":"/api/v1/jobs",
 "status":201,"duration_ms":17,
 "request_id":"9c1f0c...","level":"info","timestamp":"2026-06-19T11:36:01.234Z"}
```

In dev, the same fields are pretty-printed.

Set `JOBSVC_LOG_LEVEL=DEBUG` for SQL echo and per-pass compile
timing.

## Metrics

`GET /metrics` returns Prometheus text. The shipped collectors:

| Metric | Type | Labels | Meaning |
|--------|------|--------|---------|
| `jobsvc_jobs_total` | counter | `state` | Total jobs landing in each terminal state. |
| `jobsvc_job_duration_seconds` | histogram | `state` | End-to-end duration, claim → terminal. |
| `jobsvc_queue_depth` | gauge | — | Current number of `Queued` jobs. |
| `jobsvc_worker_lease_expirations_total` | counter | — | How often a worker lease lapsed and the row went back to `Queued`. |
| `jobsvc_errors_total` | counter | `kind` (`our` or `provider`) | Provider vs Heisenberg-side failures. |
| `calibration_refresh_total` | counter | `chip`, `ok` | Per-chip refresh success/failure. |

Standard `process_*` and `python_*` collectors are also exposed.

### Scrape config

```yaml
scrape_configs:
  - job_name: heisenberg
    static_configs:
      - targets: ["heisenberg.internal:8080"]
    metrics_path: /metrics
```

For multi-replica setups, scrape each replica separately.

## Tracing (planned)

OpenTelemetry tracing for compile-pass spans is on the roadmap. The
attachment points are already labelled in the engine
(`compile_program`, every Phonon pass, every Spinor pass) — they
emit `event=compile.span.start` / `compile.span.end` log events
today, which is enough to reconstruct a flame graph by hand.

## Audit log

The `audit_log` table records every state transition + auth event
with `user_id`, `action`, `target_id`, `detail`, `ip`, `ua`, `at`.
Query it directly:

```sql
SELECT user_id, action, target_id, detail, at
FROM audit_log
WHERE action LIKE 'job.%'
ORDER BY at DESC
LIMIT 100;
```

Or via the API:

```bash
curl -fsS http://127.0.0.1:8080/api/v1/users/me/audit \
  -H "X-API-Key: $KEY" | jq
```

## Health probes

| Endpoint | Auth | Use |
|----------|------|-----|
| `/healthz` | none | Liveness — is the process alive? |
| `/readyz`  | none | Readiness — DB reachable? |
| `/metrics` | none | Scrape target. Protect via reverse proxy ACL if exposed publicly. |

## Recommended dashboard panels

1. **Queue depth** — alarm above `N` for more than 5 minutes.
2. **p95 job duration** by `state=Completed` — track regressions.
3. **`jobsvc_errors_total{kind="our"}`** — internal failures.
4. **`jobsvc_errors_total{kind="provider"}`** — vendor failures;
   correlate with provider status pages.
5. **Calibration refresh failures** — alarm on any non-zero in a
   24-hour window.

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
