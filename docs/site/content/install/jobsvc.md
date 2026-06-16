# `jobsvc`

The Phase D FastAPI job service. Serves `/api/v1/jobs`, `/api/v1/login`,
`/api/v1/me`, `/api/v1/targets`, and friends. Stores jobs in Postgres.

## Install (developer mode)

### 1. Get the code + Python deps

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/jobsvc
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install -e ".[test]"
pip install -e ../../spinor/submit/python      # the Phase A adapter
```

### 2. Start a Postgres 17

```bash
docker run --rm -d --name jobsvc-pg -p 5432:5432 \
  -e POSTGRES_USER=jobsvc -e POSTGRES_PASSWORD=jobsvc -e POSTGRES_DB=jobsvc \
  postgres:17.10-alpine
```

### 3. Run migrations + seed an admin

```bash
export JOBSVC_DATABASE_URL=postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc
alembic upgrade head
python -m jobsvc.seed admin@local admin-password admin
```

### 4. Start the API + a worker

In two shells:

```bash
# shell 1
jobsvc                                       # uvicorn jobsvc.main:app on :8000

# shell 2
jobsvc-worker                                # claims jobs and submits them
```

### 5. Smoke test

```bash
curl -s -X POST http://localhost:8000/api/v1/login \
     -H 'Content-Type: application/json' \
     -d '{"email":"admin@local","password":"admin-password"}' | jq
```

You should receive an `access_token` + `refresh_token` pair.

## Configuration (env vars, prefix `JOBSVC_`)

| Var | Default | Purpose |
|---|---|---|
| `JOBSVC_DATABASE_URL` | `postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc` | async Postgres URL |
| `JOBSVC_JWT_SECRET` | `dev-secret-change-me` | HS256 signing key |
| `JOBSVC_JWT_ACCESS_MINUTES` | `60` | access-token TTL |
| `JOBSVC_JWT_REFRESH_DAYS` | `14` | refresh-token TTL |
| `JOBSVC_WORKER_LEASE_SECONDS` | `300` | claim lease |
| `JOBSVC_LOG_JSON` | `true` | structlog JSON output |
| `JOBSVC_CORS_ORIGINS` | `[]` | allowed CORS origins |
| `JOBSVC_SPINOR_REGISTRY_ROOT` | autodiscover | path to `spinor/registry/` |
| `SPINOR_SUBMIT_MODE` | `cassette` | submission mode (cassette/live/local) |

## Run tests

```bash
pytest tests/unit         # 37 unit tests; no DB needed
pytest tests/integration  # 35 integration tests; SQLite in-memory
pytest tests/regression   # cassette-mode end-to-end
```

Expected: **80/80 green**.

## See also

- [REST reference](../api/rest/index.html) — every endpoint.
- [Python autodoc](../api/python/jobsvc/index.md) — every public symbol.
- [Operations guide](../guide/operations.md) — production runbook.
- [Docker compose recipe](docker_compose.md) — the easy path.
