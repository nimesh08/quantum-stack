# M7 — Operations

End: a hardened, deployable, multi-user product. Two users are
isolated from each other. Provider outages are classified as
`provider` errors. The full stack comes up from one
`docker compose up`.

## Auth

- `POST /api/v1/login` — email + password → `(access, refresh)` JWT
  pair. HS256, 60-minute access, 14-day refresh.
- `Authorization: Bearer <jwt>` (Playground) or
  `X-API-Key: <prefix>.<body>` (programmatic) — the FastAPI dep
  `current_user` tries both.
- API keys: `POST /me/api-keys` returns plaintext once; bcrypt hash
  stored; lookup by 8-char `prefix`. Revoke via `DELETE`.
- Roles: `user` (default) and `admin`. Admin endpoints are
  `require_admin`-gated.
- Admin seeding via `python -m jobsvc.seed admin@local pwd admin`
  (CLI, idempotent). The compose file runs this on first start.

## Multi-tenancy

Every per-user query filters `Job.user_id == current_user.id`
before any other predicate. Admins bypass the filter; an explicit
`?user_id=` query parameter scopes admin requests.

`GET /jobs/{id}` returns 404 (not 403) for jobs the caller cannot
see — we don't leak existence.

## Observability

- **structlog** JSON logs with `request_id`, `user_id`, `job_id`,
  `provider`, `chip`, `state`. Every request has start/end logs
  bracketing it.
- **Prometheus** at `/metrics`:
  - `jobs_total{state}` — terminal-state counters.
  - `job_duration_seconds{state}`
  - `queue_depth` — gauge.
  - `worker_lease_expirations_total`
  - `provider_latency_seconds{provider}`
  - `errors_total{kind}` — `our` vs `provider`.
  - `calibration_refresh_total{chip,ok}`
- Request-id middleware accepts `x-request-id` from the caller and
  echoes it back, enabling distributed tracing.

## Deployment

`platform/deploy/` ships:

- `Dockerfile.jobsvc` (uvicorn; runs `alembic upgrade head` on start)
- `Dockerfile.worker` (same package, `jobsvc-worker` entrypoint)
- `Dockerfile.calibration` (APScheduler)
- `Dockerfile.playground` (multi-stage: vite build → nginx)
- `docker-compose.yml` — db (Postgres 17.10) + jobsvc + 2× worker +
  scheduler + playground (nginx). Bring it up with `./run.sh up`.
- `nginx.conf` — serves the SPA and proxies `/api`, `/healthz`,
  `/readyz`, `/metrics` to `jobsvc:8000`.

## Tests landed

- `tests/integration/test_isolation.py` — two-users-isolated, third
  user can't cancel, admin can cancel anyone's, /metrics names,
  request-id round-trip.

80/80 jobsvc passing at end of M7. Cumulative across the platform:
**107/107** = 80 jobsvc + 19 calibration + 8 playground.
