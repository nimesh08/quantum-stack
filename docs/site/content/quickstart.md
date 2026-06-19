# Quickstart

This page takes you from a clean machine to your first compiled
quantum job in five minutes. There are no Docker containers, no
external services, and no manual database setup. Everything runs as
a normal Python application.

## Prerequisites

- Python **3.12** or later (`python3 --version`).
- A POSIX shell (Linux, macOS, or WSL).
- Optional: `git` if you want to clone the repository for the C++ engine.

## 1. Install the launcher

```bash
pip install heisenberg
```

This pulls `heisenberg`, `jobsvc`, `calibration`, and the
`spinor-submit` adapters in one go. The C++ compiler is optional at
this stage — every cassette-mode example below works without it, and
the [C++ install guide](sdks/cpp/install.md) covers the
`spinorc` binary when you want it.

## 2. Initialise the data directory

```bash
heisenberg init
```

`init` creates `~/.local/share/heisenberg/`, generates a JWT signing
key, and runs the database migrations against the default SQLite
database `~/.local/share/heisenberg/jobsvc.db`. Re-running it is
safe — every step is idempotent.

If you would rather use Postgres, pass `--postgres`:

```bash
heisenberg init --postgres postgresql+asyncpg://user:pass@host:5432/heisenberg
```

The launcher writes that URL into the data directory's config so
subsequent `heisenberg run` invocations pick it up automatically.

## 3. Seed the default admin user

```bash
heisenberg seed
```

This creates a user `admin@local` with password `admin-password` and
prints a fresh API key — copy it now, it will not be shown again.

```text
user:    admin@local
password: admin-password   (change in the playground)
api key: Q4r2p8aA.XyZ1...32chars
```

## 4. Run the stack

```bash
heisenberg run
```

You will see something like:

```text
heisenberg 0.5.0 on http://127.0.0.1:8080/
data dir:  /home/you/.local/share/heisenberg
database:  sqlite+aiosqlite:////home/you/.local/share/heisenberg/jobsvc.db
press Ctrl-C to stop.
```

Your browser opens automatically. Log in as `admin@local /
admin-password`, click **Run**, and the default Bell program returns
a `00 / 11` histogram in under a second.

## 5. Submit from your terminal

Same job, no browser:

```bash
curl -fsS http://127.0.0.1:8080/api/v1/jobs \
  -H 'Content-Type: application/json' \
  -H "X-API-Key: $(cat ~/heisenberg-key)" \
  -d '{
    "source": "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
    "source_kind": "spinor",
    "target": "ibm_heron_r2",
    "shots": 1000
  }' | jq
```

The response carries a job UUID, the cost estimate, and the queued
state. Poll `GET /api/v1/jobs/{id}` until `state == "Completed"`.

## What just happened

```mermaid
flowchart LR
  A[".spn or .pho source"] --> B[Photon front-end]
  B --> C[Phonon optimizer]
  C --> D[Spinor place + route + decompose]
  D --> E[QASM3 / QIR / Quil]
  E --> F[provider layer]
  F --> G[Histogram in your browser]
```

Every layer ran in the same `heisenberg run` process. The provider
layer was in cassette mode by default (so no real cloud account was
charged); flip `SPINOR_SUBMIT_MODE=live` to talk to the real IBM /
AWS / Azure backends once you have credentials.

## Where to next

- Write your first program: pick a [language](languages/index.md).
- Build something on top of the API: read the
  [SDKs](sdks/index.md) — Python, C++, TypeScript, REST.
- Move to a real server:
  [Operations / Native systemd](operations/native_systemd.md).
- Stop and clean up: `heisenberg stop`. Your data directory at
  `~/.local/share/heisenberg/` survives across runs; delete it to
  reset everything.
