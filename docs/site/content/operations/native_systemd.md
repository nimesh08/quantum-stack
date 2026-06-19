# Operations — native systemd

Three units run the production stack:

- `heisenberg-jobsvc.service` — FastAPI surface.
- `heisenberg-worker.service` — queue worker (separate process for
  SIGSEGV isolation).
- `heisenberg-calibration.service` — nightly refresh scheduler.

The full install path is in [Operations / Install](install.md). This
page is the reference for the three unit files.

## File tree on a production host

```text
/etc/heisenberg/
└── heisenberg.env             # shared config; mode 640, owner heisenberg
/etc/systemd/system/
├── heisenberg-jobsvc.service
├── heisenberg-worker.service
└── heisenberg-calibration.service
/opt/heisenberg/
└── venv/                      # virtualenv with `heisenberg` installed
/usr/share/heisenberg/
└── registry/                  # spinor chip YAMLs
/var/lib/heisenberg/           # data directory; mode 750, owner heisenberg
├── jobsvc.db                  # SQLite (if you didn't pick Postgres)
├── jwt.secret
├── heisenberg.pid
├── heisenberg.log
└── calibration/               # per-chip nightly snapshots
```

## heisenberg.env

The shared environment file every unit reads. Mode 640, owner
`heisenberg:heisenberg`. Sample content:

```bash
HEISENBERG_DATA_DIR=/var/lib/heisenberg
SPINOR_REGISTRY_ROOT=/usr/share/heisenberg/registry
SPINOR_SUBMIT_MODE=cassette
JOBSVC_LOG_JSON=true
JOBSVC_LOG_LEVEL=INFO

# Pin a stable JWT signing key in production.
JOBSVC_JWT_SECRET=please-change-me

# Postgres if you want it.
# JOBSVC_DATABASE_URL=postgresql+asyncpg://heisenberg:CHANGEME@localhost:5432/heisenberg
```

## heisenberg-jobsvc.service

```ini
[Unit]
Description=Heisenberg quantum stack -- jobsvc (FastAPI)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=heisenberg
Group=heisenberg
WorkingDirectory=/var/lib/heisenberg
EnvironmentFile=/etc/heisenberg/heisenberg.env
ExecStart=/opt/heisenberg/venv/bin/heisenberg run \
    --production --no-worker --no-scheduler \
    --host 127.0.0.1 --port 8080
Restart=on-failure
RestartSec=3
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/heisenberg
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
LockPersonality=true
RestrictNamespaces=true
RestrictRealtime=true
RestrictSUIDSGID=true
SystemCallArchitectures=native

[Install]
WantedBy=multi-user.target
```

Key flags:

- `--production` — JSON logs, bind to the network.
- `--no-worker --no-scheduler` — the other two units handle those;
  the FastAPI process should not multiplex.
- The `Restrict*` and `Protect*` directives are kernel-level
  hardening; they constrain what the process can touch.

## heisenberg-worker.service

```ini
[Unit]
Description=Heisenberg quantum stack -- queue worker
After=heisenberg-jobsvc.service
Requires=heisenberg-jobsvc.service

[Service]
Type=simple
User=heisenberg
Group=heisenberg
WorkingDirectory=/var/lib/heisenberg
EnvironmentFile=/etc/heisenberg/heisenberg.env
ExecStart=/opt/heisenberg/venv/bin/jobsvc-worker
Restart=on-failure
RestartSec=3
# (same hardening block as jobsvc.service)
```

Multiple replicas are supported: just run several copies of this
unit on different hosts (or rename to
`heisenberg-worker@.service` and use template instances). Postgres
+ `FOR UPDATE SKIP LOCKED` ensures each job is claimed exactly
once.

## heisenberg-calibration.service

```ini
[Unit]
Description=Heisenberg quantum stack -- calibration scheduler
After=network-online.target heisenberg-jobsvc.service
Wants=network-online.target

[Service]
Type=simple
User=heisenberg
Group=heisenberg
WorkingDirectory=/var/lib/heisenberg
EnvironmentFile=/etc/heisenberg/heisenberg.env
ExecStart=/opt/heisenberg/venv/bin/calibration
Restart=on-failure
RestartSec=15
# (hardening)
```

This is the only "long-running idle" unit — it sits in an
APScheduler `BlockingScheduler` and fires once per night. Restart
back-off is longer (15 s) because frequent failure to start usually
means a config issue.

## Verifying

```bash
sudo systemctl status heisenberg-jobsvc heisenberg-worker heisenberg-calibration
sudo journalctl -u heisenberg-jobsvc -f
sudo journalctl -u heisenberg-worker -n 100
```

If `heisenberg-jobsvc` is up but `heisenberg-worker` is in a crash
loop, the most likely cause is a missing `SPINOR_REGISTRY_ROOT` or
a bad `JOBSVC_DATABASE_URL`. Check
[Troubleshooting](troubleshooting.md).

## Updating

```bash
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install -U heisenberg
sudo -u heisenberg \
  env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
  /opt/heisenberg/venv/bin/heisenberg init      # idempotent migration
sudo systemctl restart heisenberg-jobsvc \
                       heisenberg-worker \
                       heisenberg-calibration
```

The migrations are idempotent; reruns are safe.

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
