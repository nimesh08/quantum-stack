# Server (systemd) — production deployment

The recommended way to run the platform on your own server. No Docker.
Pure native: Postgres 17 from `apt`, Python 3.12, Node 22, nginx,
systemd, Let's Encrypt.

If you just want a 60-second laptop setup, see
[Docker compose](docker_compose.md) — it stays in the repo as a
developer convenience.

---

## What this builds

```mermaid
flowchart LR
    user["Browser"] --> nginx["nginx :443<br/>(systemd)"]
    nginx -- "/" --> spa["Playground SPA<br/>(static files)"]
    nginx -- "/api/*" --> jobsvc["jobsvc :8000<br/>(systemd)"]
    jobsvc --> pg["Postgres 17<br/>(systemd)"]
    workers["jobsvc-worker@1..N<br/>(systemd template)"] --> pg
    sched["calibration<br/>(systemd)"] -. nightly .-> pg
```

One server, one TLS cert, no CORS. Everything talks over `127.0.0.1`
inside the box; nginx handles HTTPS at the edge.

---

## 1. Prerequisites

| Tool | Version | Source |
|---|---|---|
| Ubuntu | 24.04 LTS | or Debian 12 (bookworm) |
| Python | 3.12 | `apt install python3.12 python3.12-venv` |
| Node | 22.x | nvm or nodesource |
| PostgreSQL | 17.10 | `apt install postgresql-17` (Ubuntu 24.04 ships 16; add the [PGDG repo](https://www.postgresql.org/download/linux/ubuntu/) for 17) |
| nginx | 1.24+ | `apt install nginx` |
| certbot | latest | `apt install certbot python3-certbot-nginx` |
| build deps | — | `apt install build-essential cmake ninja-build g++-13 libeigen3-dev libpq-dev git` |

A 1 vCPU / 2 GB / 20 GB VPS is enough to start.

---

## 2. System users

The platform runs under unprivileged users:

```bash
sudo useradd --system --shell /usr/sbin/nologin --home /opt/quantum-stack quantum
sudo usermod -aG quantum jobsvc 2>/dev/null || sudo useradd --system --shell /usr/sbin/nologin --home /opt/quantum-stack jobsvc
sudo install -d -o quantum -g quantum /opt/quantum-stack
sudo install -d -o jobsvc -g jobsvc -m 750 /etc/quantum-stack
sudo install -d -o jobsvc -g jobsvc -m 750 /var/cache/spinor/calibration
```

---

## 3. Postgres 17

### Install (Ubuntu 24.04 + PGDG)

```bash
sudo apt install -y curl ca-certificates
sudo install -d /usr/share/postgresql-common/pgdg
sudo curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
     -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc
echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] \
https://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" \
    | sudo tee /etc/apt/sources.list.d/pgdg.list
sudo apt update
sudo apt install -y postgresql-17 postgresql-contrib-17
```

### Bootstrap the DB

```bash
sudo -u postgres psql <<SQL
CREATE ROLE jobsvc WITH LOGIN PASSWORD 'change-me-in-production';
CREATE DATABASE jobsvc OWNER jobsvc;
SQL
```

(Make `'change-me-in-production'` a long random secret. Put the
matching string in `JOBSVC_DATABASE_URL` further down.)

---

## 4. Clone + build

As `quantum`:

```bash
sudo -u quantum -H bash <<'EOF'
cd /opt
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
EOF
```

### 4a. Build the C++ engine

```bash
sudo -u quantum -H bash <<'EOF'
cd /opt/quantum-stack
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
EOF
```

This produces `build/spinor/cli/spinorc` and the
`build/photon/bindings/python/_engine*.so` nanobind extension.

### 4b. Python virtualenv with all packages

```bash
sudo -u quantum -H bash <<'EOF'
cd /opt/quantum-stack
python3.12 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install nanobind 'bcrypt>=3.2,<4.1'
.venv/bin/pip install -e platform/jobsvc
.venv/bin/pip install -e platform/calibration
.venv/bin/pip install -e spinor/submit/python
.venv/bin/pip install -e photon/frontends/python
EOF
```

### 4c. Build the playground SPA

```bash
sudo -u quantum -H bash <<'EOF'
cd /opt/quantum-stack/platform/playground
npm install
npm run build      # produces dist/
EOF
```

The static SPA lives at `/opt/quantum-stack/platform/playground/dist/`.
nginx will serve it directly.

---

## 5. Configure environment

Copy the template and edit:

```bash
sudo install -m 640 -o jobsvc -g jobsvc \
     /opt/quantum-stack/platform/deploy/.env.production.example \
     /etc/quantum-stack/jobsvc.env
sudo -u jobsvc vim /etc/quantum-stack/jobsvc.env
```

Required edits:

```bash
JOBSVC_DATABASE_URL=postgresql+asyncpg://jobsvc:change-me-in-production@127.0.0.1:5432/jobsvc
JOBSVC_JWT_SECRET=<run `openssl rand -hex 32` and paste here>
JOBSVC_LOG_JSON=true
JOBSVC_SPINOR_REGISTRY_ROOT=/opt/quantum-stack/spinor/registry
SPINOR_SUBMIT_MODE=cassette          # or `live` once you have provider creds
PYTHONPATH=/opt/quantum-stack/build/photon/bindings/python
HOME=/var/cache                      # so calibration writes the right path
```

---

## 6. Systemd units

The repo ships four units under
[`platform/deploy/systemd/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/deploy/systemd).
Install them:

```bash
sudo install -m 644 \
  /opt/quantum-stack/platform/deploy/systemd/jobsvc.service \
  /opt/quantum-stack/platform/deploy/systemd/jobsvc-worker@.service \
  /opt/quantum-stack/platform/deploy/systemd/calibration.service \
  /etc/systemd/system/

sudo systemctl daemon-reload
```

### Run alembic + seed once

```bash
sudo -u jobsvc -H bash <<'EOF'
cd /opt/quantum-stack/platform/jobsvc
set -a && . /etc/quantum-stack/jobsvc.env && set +a
/opt/quantum-stack/.venv/bin/alembic upgrade head
/opt/quantum-stack/.venv/bin/python -m jobsvc.seed admin@local change-this-password admin
EOF
```

### Start the services

```bash
sudo systemctl enable --now jobsvc.service
sudo systemctl enable --now jobsvc-worker@1.service jobsvc-worker@2.service
sudo systemctl enable --now calibration.service
sudo systemctl status jobsvc jobsvc-worker@1 jobsvc-worker@2 calibration
```

---

## 7. nginx + Let's Encrypt

```bash
sudo install -m 644 \
  /opt/quantum-stack/platform/deploy/nginx/quantum-stack.conf \
  /etc/nginx/sites-available/quantum-stack
sudo ln -sf /etc/nginx/sites-available/quantum-stack \
            /etc/nginx/sites-enabled/quantum-stack
sudo rm -f /etc/nginx/sites-enabled/default

# Edit the server_name to your hostname:
sudo sed -i "s/yourserver.example.com/$(hostname -f)/g" \
            /etc/nginx/sites-available/quantum-stack

sudo nginx -t && sudo systemctl reload nginx
```

### Obtain a TLS cert

```bash
sudo certbot --nginx -d $(hostname -f) --redirect --non-interactive \
             --agree-tos -m you@example.com
```

certbot rewrites the nginx config to listen on `:443` with the cert and
redirects `:80` → `:443`. Renewal is automatic via the certbot timer.

---

## 8. Smoke test

```bash
curl -fsS https://$(hostname -f)/healthz | jq
# {"status":"ok","version":"0.4.0+phased.m1","engine_available":true}

# Login
TOKEN=$(curl -s -X POST https://$(hostname -f)/api/v1/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"admin@local","password":"change-this-password"}' | jq -r .access_token)

# Submit a Bell program
curl -s -X POST https://$(hostname -f)/api/v1/jobs \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"source":"target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
       "source_kind":"spinor","target":"generic","shots":100}' | jq
```

Then open `https://your.server.com/` in a browser. You should see the
playground.

---

## 9. Updates

```bash
sudo -u quantum -H bash <<'EOF'
cd /opt/quantum-stack
git pull
cmake --build build -j
.venv/bin/pip install -e platform/jobsvc -e platform/calibration \
                     -e spinor/submit/python -e photon/frontends/python
cd platform/playground && npm install && npm run build
EOF

sudo -u jobsvc -H bash <<'EOF'
cd /opt/quantum-stack/platform/jobsvc
set -a && . /etc/quantum-stack/jobsvc.env && set +a
/opt/quantum-stack/.venv/bin/alembic upgrade head
EOF

sudo systemctl restart jobsvc.service jobsvc-worker@1 jobsvc-worker@2 calibration.service
sudo systemctl reload nginx
```

---

## 10. Live providers (optional)

To switch from cassette to live mode, edit `/etc/quantum-stack/jobsvc.env`:

```bash
SPINOR_SUBMIT_MODE=live

# IBM
IBM_QUANTUM_TOKEN=...

# AWS Braket — uses ~/.aws/credentials of the jobsvc user
# Place creds at /var/lib/jobsvc/.aws/credentials with role 'default'.

# Azure Quantum
AZURE_QUANTUM_RESOURCE_ID=/subscriptions/.../workspaces/...
AZURE_QUANTUM_LOCATION=westus
```

then `sudo systemctl restart jobsvc-worker@*`.

---

## 11. Common errors

| Symptom | Fix |
|---|---|
| `systemctl status jobsvc` shows `(code=exited, status=1/FAILURE)` | `journalctl -u jobsvc -n 50` — usually `JOBSVC_DATABASE_URL` typo or Postgres not reachable |
| `502 Bad Gateway` from nginx | `jobsvc` not running yet; `systemctl restart jobsvc` and wait 2s |
| Login banner says "invalid credentials" | seed didn't run; `sudo -u jobsvc /opt/quantum-stack/.venv/bin/python -m jobsvc.seed admin@local pwd admin` |
| `photon._engine` import fails | `PYTHONPATH=/opt/quantum-stack/build/photon/bindings/python` missing from env file |
| `alembic.util.exc.CommandError: Can't locate revision` | DB out of date; `alembic upgrade head` |
| `Failed to connect to Postgres` | check `pg_hba.conf` allows `127.0.0.1` for the `jobsvc` user; `psql -h 127.0.0.1 -U jobsvc -d jobsvc` to verify |

---

## 12. Pattern B — playground on GitHub Pages, API on your server

If you'd rather have the playground live at
`https://nimesh08.github.io/quantum-stack/playground/` and only the API
on your server:

1. **Build the playground with a different API base** —
   `VITE_API_BASE=https://your.server.com npm run build` —
   instead of `npm run build` in step 4c. Commit the resulting
   `dist/` to GitHub Pages (separate workflow; not part of this run).
2. **Allow CORS** in `/etc/quantum-stack/jobsvc.env`:
   ```bash
   JOBSVC_CORS_ORIGINS=["https://nimesh08.github.io"]
   ```
3. Restart `jobsvc.service`.

`Pattern A` (nginx serving the SPA on your server) is what this runbook
defaults to — simpler, no CORS.

---

## See also

- [Operations runbook](../guide/operations.md) — logs, metrics, scaling
- [`jobsvc` API reference](../api/rest/index.html)
- [Troubleshooting](troubleshooting.md)
- [Docker compose (laptop dev)](docker_compose.md) — alternative
