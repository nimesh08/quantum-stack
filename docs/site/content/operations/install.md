# Operations — install

Install Heisenberg on a server.

## What we are installing

- A dedicated `heisenberg` system user.
- A virtual environment at `/opt/heisenberg/venv` with the
  `heisenberg` package and its transitive deps.
- A data directory at `/var/lib/heisenberg/`.
- A config file at `/etc/heisenberg/heisenberg.env`.
- Three systemd units:
  - `heisenberg-jobsvc.service` — FastAPI surface.
  - `heisenberg-worker.service` — queue worker.
  - `heisenberg-calibration.service` — nightly refresh.

## Prerequisites

- Linux with systemd (Ubuntu 24.04, Debian 13, or RHEL 9).
- Python 3.12 or 3.13.
- Optional: PostgreSQL 17.10 if you want Postgres instead of
  SQLite.
- A reverse proxy (nginx, Caddy, traefik) if you want TLS or
  multiple replicas.

## Step 1 — create the system user

```bash
sudo useradd --system --home /var/lib/heisenberg \
             --shell /usr/sbin/nologin heisenberg
sudo install -d -m 750 -o heisenberg -g heisenberg /var/lib/heisenberg
sudo install -d -m 750 -o heisenberg -g heisenberg /etc/heisenberg
```

## Step 2 — install heisenberg into a dedicated venv

```bash
sudo install -d -o heisenberg -g heisenberg /opt/heisenberg
sudo -u heisenberg python3 -m venv /opt/heisenberg/venv
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install \
  --upgrade pip wheel
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install heisenberg
```

For Postgres, replace the last line with:

```bash
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install 'heisenberg[postgres]'
```

## Step 3 — drop the chip registry where the launcher can find it

```bash
sudo install -d /usr/share/heisenberg
git clone --depth 1 https://github.com/nimesh08/quantum-stack.git /tmp/qs
sudo cp -r /tmp/qs/spinor/registry /usr/share/heisenberg/registry
sudo chown -R root:root /usr/share/heisenberg
```

The launcher's `find_spinor_registry()` searches a fixed list of
paths; `/usr/share/heisenberg/registry` is one of them.

## Step 4 — configure

```bash
sudo cp /tmp/qs/platform/systemd/heisenberg.env.example \
        /etc/heisenberg/heisenberg.env
sudo chown heisenberg:heisenberg /etc/heisenberg/heisenberg.env
sudo chmod 640 /etc/heisenberg/heisenberg.env
sudoedit /etc/heisenberg/heisenberg.env
```

Set at minimum:

```bash
HEISENBERG_DATA_DIR=/var/lib/heisenberg
SPINOR_REGISTRY_ROOT=/usr/share/heisenberg/registry
JOBSVC_LOG_JSON=true
SPINOR_SUBMIT_MODE=cassette       # or 'live' if you have credentials
```

For Postgres, also set:

```bash
JOBSVC_DATABASE_URL=postgresql+asyncpg://heisenberg:CHANGEME@localhost:5432/heisenberg
```

## Step 5 — initialise + seed

```bash
sudo -u heisenberg \
  env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
  /opt/heisenberg/venv/bin/heisenberg init

sudo -u heisenberg \
  env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
  /opt/heisenberg/venv/bin/heisenberg seed
```

Save the API key the second command prints — it will not be shown
again.

## Step 6 — install the systemd units

```bash
sudo install -m 644 /tmp/qs/platform/systemd/heisenberg-*.service \
                    /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now heisenberg-jobsvc \
                            heisenberg-worker \
                            heisenberg-calibration
```

## Step 7 — verify

```bash
sudo systemctl status heisenberg-jobsvc heisenberg-worker heisenberg-calibration
curl -fsS http://127.0.0.1:8080/healthz
curl -fsS http://127.0.0.1:8080/readyz
curl -fsS http://127.0.0.1:8080/api/v1/targets | jq '.items | length'
```

`length` should equal the chip count (27).

## Step 8 — reverse proxy (optional)

For TLS, add nginx or Caddy in front of `127.0.0.1:8080`. A minimal
nginx site:

```nginx
server {
  listen 443 ssl http2;
  server_name heisenberg.example.com;

  ssl_certificate     /etc/letsencrypt/live/heisenberg.example.com/fullchain.pem;
  ssl_certificate_key /etc/letsencrypt/live/heisenberg.example.com/privkey.pem;

  location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host              $host;
    proxy_set_header X-Real-IP         $remote_addr;
    proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
  }
}
```

## Where to next

- [Native systemd](native_systemd.md) — the unit files explained.
- [Postgres](postgres.md) — when and how to switch off SQLite.
- [Observability](observability.md) — Prometheus, structured logs.
- [Troubleshooting](troubleshooting.md).

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
