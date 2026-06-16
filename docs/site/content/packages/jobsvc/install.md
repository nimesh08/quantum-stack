# Install — `jobsvc`

The Phase D FastAPI service. The recommended production deployment is
[Server (systemd)](../../install/server_systemd.md). For laptop dev:

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/jobsvc
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install -e ".[test]"
pip install -e ../../spinor/submit/python

docker run --rm -d --name jobsvc-pg -p 5432:5432 \
  -e POSTGRES_USER=jobsvc -e POSTGRES_PASSWORD=jobsvc -e POSTGRES_DB=jobsvc \
  postgres:17.10-alpine

export JOBSVC_DATABASE_URL=postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc
alembic upgrade head
python -m jobsvc.seed admin@local admin-password admin
jobsvc                       # uvicorn on :8000
```

In another shell:

```bash
jobsvc-worker
```

## Smoke test

```bash
curl -s -X POST http://localhost:8000/api/v1/login \
     -H 'Content-Type: application/json' \
     -d '{"email":"admin@local","password":"admin-password"}' | jq
```

## See also

- [REST reference](../../api/rest/index.html)
- [Python autodoc](../../api/python/jobsvc/index.md)
- [Cookbook](cookbook.md)
- [SDK examples](sdk_examples.md)
- [Operations runbook](../../guide/operations.md)
