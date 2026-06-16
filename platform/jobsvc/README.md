# Job service (Phase D)

FastAPI front for the Photon/Phonon/Spinor compiler engine.

## Quickstart

```bash
cd platform/jobsvc
python -m venv .venv && source .venv/bin/activate
pip install -e ".[test]"

# Start a Postgres 17 container in another shell:
docker run --rm -p 5432:5432 \
  -e POSTGRES_USER=jobsvc -e POSTGRES_PASSWORD=jobsvc -e POSTGRES_DB=jobsvc \
  postgres:17.10-alpine

export JOBSVC_DATABASE_URL=postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc
alembic upgrade head
jobsvc            # API on :8000
jobsvc-worker     # in another shell
```

## Run the test suite

```bash
pytest                    # everything
pytest tests/unit         # fast, no DB
pytest -m integration     # needs a Postgres
```

See `../../docs/build/phaseD_platform_guide.md` for the full guide.
