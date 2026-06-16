# Install — `calibration`

The Phase D nightly refresh service.

## Quickstart (developer)

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/calibration
python3.12 -m venv .venv && source .venv/bin/activate
pip install -e ".[test]"
calibration --once
```

## Production

Use the systemd unit shipped with the repo:

```bash
sudo install -m 644 /opt/quantum-stack/platform/deploy/systemd/calibration.service \
                    /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now calibration
```

See [Server runbook §6](../../install/server_systemd.md) for the
complete setup.

## See also

- [Cookbook](cookbook.md) — writing your own provider
- [Python autodoc](../../api/python/calibration/index.md)
- [Tutorial: Add a provider](../../tutorial/add_a_provider.md)
