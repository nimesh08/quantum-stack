# Heisenberg systemd units

Three native-systemd units for production deployments:

- `heisenberg-jobsvc.service`    - the FastAPI service (uvicorn).
- `heisenberg-worker.service`    - the queue worker.
- `heisenberg-calibration.service` - the nightly calibration scheduler.

The `--separate-processes` model: each unit runs `heisenberg run
--production --no-worker --no-scheduler` (jobsvc only) or
`python -m jobsvc.worker` / `calibration` directly. They all read
`/etc/heisenberg/heisenberg.env` for shared configuration (database
URL, JWT secret, registry path).

## Install

```bash
# 1. Create the service user.
sudo useradd --system --home /var/lib/heisenberg \
    --shell /usr/sbin/nologin heisenberg

# 2. Install the heisenberg package into a dedicated venv.
sudo install -d -o heisenberg -g heisenberg /opt/heisenberg
sudo -u heisenberg python3 -m venv /opt/heisenberg/venv
sudo -u heisenberg /opt/heisenberg/venv/bin/pip install heisenberg

# 3. Drop config + units into place.
sudo install -d -m 750 -o heisenberg -g heisenberg /etc/heisenberg
sudo install -m 640 -o heisenberg -g heisenberg \
    heisenberg.env.example /etc/heisenberg/heisenberg.env
sudo install -m 644 *.service /etc/systemd/system/

# 4. Initialise + seed (one-time).
sudo -u heisenberg env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
    /opt/heisenberg/venv/bin/heisenberg init
sudo -u heisenberg env $(grep -v '^#' /etc/heisenberg/heisenberg.env | xargs) \
    /opt/heisenberg/venv/bin/heisenberg seed

# 5. Start services.
sudo systemctl daemon-reload
sudo systemctl enable --now heisenberg-jobsvc heisenberg-worker \
    heisenberg-calibration
```

## Logs

```bash
sudo journalctl -u heisenberg-jobsvc -f
sudo journalctl -u heisenberg-worker -f
sudo journalctl -u heisenberg-calibration -f
```

Author: Nimesh Cheedella.
