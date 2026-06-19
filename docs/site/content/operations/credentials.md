# Operations — Provider credentials

Every cloud provider needs a different set of environment variables.
Set them in `/etc/heisenberg/heisenberg.env` (production) or your
shell rc (development), then flip `SPINOR_SUBMIT_MODE=live`.

In `cassette` mode (the default) no credentials are needed.

## IBM Quantum

```bash
SPINOR_SUBMIT_MODE=live
IBM_QUANTUM_TOKEN=<your-IBM-Quantum-runtime-token>
```

Get a token at <https://quantum.cloud.ibm.com/>. The `qiskit-ibm-runtime`
SDK reads `IBM_QUANTUM_TOKEN` automatically.

Free-tier users have a fixed monthly quota; combine with Heisenberg's
budget gate to avoid surprises.

## AWS Braket

Standard AWS credentials. Either set the env vars:

```bash
SPINOR_SUBMIT_MODE=live
AWS_ACCESS_KEY_ID=...
AWS_SECRET_ACCESS_KEY=...
AWS_DEFAULT_REGION=us-east-1
```

Or rely on the default AWS credential chain
(`~/.aws/credentials`, IAM role, IMDS). The `amazon-braket-sdk`
honours the same chain.

## Azure Quantum

```bash
SPINOR_SUBMIT_MODE=live
AZURE_QUANTUM_RESOURCE_ID=/subscriptions/.../providers/Microsoft.Quantum/Workspaces/...
AZURE_QUANTUM_LOCATION=eastus
```

Auth is via the Azure default credential chain; install the Azure
CLI and `az login` on the host.

## QCI Aqumen, Anyon Yukon, TII Falcon, Alice and Bob Boson 4

These four adapters ship in **cassette mode only** today; their
production submission URLs are not publicly documented. Live mode
raises `RuntimeError` pointing at the
[unsupported-chips ledger](../internals/chips_unsupported.md).

When the upstream vendor publishes their REST URL, one PR per row
unblocks them.

## Where to drop the env file

| Install shape | File |
|---------------|------|
| Laptop dev | export in your shell rc / direnv |
| `heisenberg run` ad-hoc | `~/.config/heisenberg/heisenberg.env` (the launcher reads it) |
| systemd | `/etc/heisenberg/heisenberg.env`; mode `640`, owner `heisenberg:heisenberg` |

The file is plain `KEY=VALUE` per line. Quoting follows shell rules.

## Rotating credentials

1. Edit the env file.
2. `sudo systemctl restart heisenberg-jobsvc heisenberg-worker
   heisenberg-calibration`.
3. Verify with a cassette-mode replay:
   `SPINOR_SUBMIT_MODE=cassette curl ... /api/v1/jobs ...`.
4. Flip to `live` and submit a small (e.g. 10-shot) probe.

If a credential is leaked, rotate it at the provider, then update
the env file. Heisenberg never persists the credentials to disk;
only the running process holds them in memory.

---

Heisenberg's operations layer was designed and implemented by **Nimesh Cheedella**.
