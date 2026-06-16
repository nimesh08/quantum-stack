# Docker compose

> **For production**, see [Server (systemd)](server_systemd.md). This
> page covers the laptop-developer convenience path.

Bring up the entire Phase D platform on your laptop with one command.
Five services: Postgres 17.10, jobsvc, two workers, calibration
scheduler, playground.

## Prerequisites

- Docker Engine 25+
- Docker Compose v2 (ships with modern Docker)
- About 2 GB free disk

## Start

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/deploy
cp .env.example .env
./run.sh up -d
```

First start takes ~2 minutes (image build + alembic migrations).

## Wait until healthy

```bash
./run.sh smoke
# OK
```

That polls `/healthz` until it returns 200.

## Open the playground

<http://localhost:8080> — log in with the seeded admin:

| Field | Value |
|---|---|
| Email | `admin@local` |
| Password | `admin-password` |

The Bell program is preloaded; click **Run**.

## Run.sh subcommands

```bash
./run.sh up -d       # build + start
./run.sh down        # stop + drop volumes (destroys data)
./run.sh smoke       # poll healthz; exit 0 when up
./run.sh ps          # list containers
./run.sh logs        # stream all logs
./run.sh logs jobsvc # only jobsvc
```

## Configure live mode

Edit `.env`:

```bash
SPINOR_SUBMIT_MODE=live
JWT_SECRET=<a long random string>

# IBM
IBM_QUANTUM_TOKEN=...
```

then `./run.sh up -d --build` to rebuild.

## Services

| Service | Port | Purpose |
|---|---|---|
| `db` | `5432` | Postgres 17.10 |
| `jobsvc` | `8000` | FastAPI |
| `worker` | — | × 2 by default |
| `scheduler` | — | nightly calibration |
| `playground` | `8080` | nginx → SPA + reverse-proxies `/api` |

## Smoke from the API

```bash
TOKEN=$(curl -s -X POST http://localhost:8000/api/v1/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"admin@local","password":"admin-password"}' | jq -r .access_token)

curl -s -X POST http://localhost:8000/api/v1/jobs \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
        "source": "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
        "source_kind": "spinor",
        "target": "generic",
        "shots": 100
      }' | jq
```

## Tear down

```bash
./run.sh down
```

Destroys the Postgres volume. Run again to start fresh.

## See also

- [Operations runbook](../guide/operations.md)
- [`jobsvc` install](jobsvc.md)
- [Troubleshooting](troubleshooting.md)
