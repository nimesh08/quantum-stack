# Operations — Postgres

Heisenberg defaults to SQLite for laptop and small-team installs.
For production scale, switch to Postgres. The schema is
dialect-aware, so every Heisenberg test passes against both — the
two are first-class targets, not "Postgres for production, SQLite
for tests".

## When to use Postgres

| Signal | Switch to Postgres |
|--------|---------------------|
| > 1 worker replica | required (SQLite can't lock-skip across processes) |
| > 1 jobsvc replica | required |
| Job rate > 50 / sec | recommended (SQLite serialises writes) |
| Database > 10 GiB | recommended |
| You need point-in-time-recovery / WAL backups | required |

If none of those apply, stay on SQLite. It is genuinely faster for
the laptop case and the schema is identical.

## Prerequisites

- PostgreSQL 17.10 (the pin) — versions 16+ work but are not in CI.
- The `heisenberg[postgres]` extra (pulls `asyncpg`).

## Step 1 — install Postgres

```bash
sudo apt install postgresql-17
```

On Debian 13:

```bash
sudo systemctl enable --now postgresql
```

## Step 2 — create the database and user

```bash
sudo -u postgres psql <<'SQL'
CREATE USER heisenberg WITH PASSWORD 'CHANGE-ME';
CREATE DATABASE heisenberg OWNER heisenberg ENCODING 'UTF8';
GRANT ALL PRIVILEGES ON DATABASE heisenberg TO heisenberg;
SQL
```

## Step 3 — point Heisenberg at it

In `/etc/heisenberg/heisenberg.env`:

```bash
JOBSVC_DATABASE_URL=postgresql+asyncpg://heisenberg:CHANGE-ME@localhost:5432/heisenberg
```

## Step 4 — install + initialise

```bash
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install -U 'heisenberg[postgres]'
sudo -u heisenberg \
  env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
  /opt/heisenberg/venv/bin/heisenberg init
```

`heisenberg init` runs `alembic upgrade head` against the new
database. The schema is dialect-aware: on Postgres you get native
`uuid` and `JSONB` columns; on SQLite, the same columns become
`CHAR(36)` and `JSON`.

## Step 5 — restart the systemd units

```bash
sudo systemctl restart heisenberg-jobsvc \
                       heisenberg-worker \
                       heisenberg-calibration
sudo journalctl -u heisenberg-jobsvc -n 50
```

## Migrating an existing SQLite database to Postgres

There is no first-class migration tool today. Recommended approach:

1. Stop all `heisenberg-*` units.
2. Use `pgloader` to copy SQLite to Postgres (it handles type
   coercions; SQLite UUID strings convert to Postgres `uuid` cleanly).
3. Re-run `heisenberg init` to apply any pending migrations on the
   Postgres database.
4. Start the units.

A future release will ship `heisenberg migrate-database --from
sqlite --to postgres` for this; track the issue at the GitHub repo.

## Tuning

For Heisenberg's workload (short transactions, mostly small JSON
blobs in the `results.counts` column), the defaults are fine.
Things you might tune as the volume grows:

- `max_connections` — set to `2 * (jobsvc replicas + worker replicas)`
  + 10 buffer.
- `shared_buffers` — 25% of RAM.
- `work_mem` — 16 MB if `EXPLAIN` shows external sorts.
- `pg_stat_statements` — enable for query-level metrics.

## Backups

```bash
pg_dump --format=custom --compress=9 \
        -h localhost -U heisenberg heisenberg \
        > heisenberg-$(date +%F).dump
```

Restore:

```bash
pg_restore --clean --if-exists \
           -h localhost -U heisenberg -d heisenberg \
           heisenberg-2026-06-19.dump
```

Postgres native PITR / WAL streaming is recommended for production;
both are out of scope for this page.

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
