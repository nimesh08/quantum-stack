---
title: CLI install (photonc, phononc, spinorc)
description: One-line install of the Heisenberg CLI binaries with provenance verification via gh attestation.
---

# Install the CLI binaries

Three driver binaries make up the Heisenberg toolchain, mirroring the
gcc / cc1 / as layered design:

| Binary    | Layer it drives        | Subprocess it shells out to    |
|-----------|------------------------|--------------------------------|
| `photonc` | Photon (top)           | `phononc` -> `spinorc` -> `python -m spinor_submit` |
| `phononc` | Phonon (middle)        | `spinorc` -> `python -m spinor_submit`              |
| `spinorc` | Spinor (bottom)        | `python -m spinor_submit`                           |

You normally run only `photonc`; it dispatches to the lower layers
automatically. `phononc` is the right tool when you write Phonon IR by
hand; `spinorc` is for hand-written Spinor.

---

## One-line install

### Linux (x86_64 or aarch64)

```bash
curl -sSL https://nimesh08.github.io/quantum-stack/install.sh | bash
```

The script:

1. Detects your host triple (`linux-x86_64` or `linux-aarch64`).
2. Resolves the latest release from <https://github.com/nimesh08/quantum-stack/releases>.
3. Downloads `heisenberg-cli-<version>-<triple>.tar.gz` from the docs site mirror.
4. Verifies provenance via `gh attestation verify --repo nimesh08/quantum-stack`. Falls back to `sha256sum -c` if `gh` is missing. (Override with `--no-verify`, NOT recommended.)
5. Installs to `~/.local/heisenberg/` and symlinks `~/.local/bin/{spinorc,photonc,phononc,spinor-submit}`.
6. Writes `~/.local/heisenberg/env.sh` exporting `QSTACK_PYTHONPATH` and `QSTACK_SPINORC` for the C++ binaries' subprocess hop.

### Windows (x86_64)

```powershell
iwr -useb https://nimesh08.github.io/quantum-stack/install.ps1 | iex
```

Installs to `%LOCALAPPDATA%\heisenberg\`.

### Override version or install location

```bash
# Pin a specific version.
curl -sSL .../install.sh | bash -s -- --version v0.1.0

# Custom prefix (default $HOME/.local).
curl -sSL .../install.sh | bash -s -- --prefix /opt/heisenberg
```

---

## What gets installed

```
~/.local/heisenberg/
|-- bin/
|   |-- spinorc           Phase A driver
|   |-- photonc           top driver (the one users normally use)
|   `-- phononc           middle driver
|-- python/
|   `-- spinor_submit/    pure-Python provider adapter (IBM/AWS/Azure/QCI/Anyon/TII/Alice & Bob)
|-- env.sh                exports QSTACK_PYTHONPATH + QSTACK_SPINORC
|-- README.md
`-- FUTUREPLAN.md

~/.local/bin/
|-- photonc -> .../heisenberg/bin/photonc
|-- phononc -> .../heisenberg/bin/phononc
|-- spinorc -> .../heisenberg/bin/spinorc
`-- spinor-submit         python launcher (no virtualenv needed)
```

---

## Verifying provenance manually

Every release is signed via [Sigstore](https://www.sigstore.dev/) using
GitHub's OIDC-backed attestation flow. To verify a downloaded artifact:

```bash
# Using gh (the GitHub CLI):
gh attestation verify heisenberg-cli-v0.1.0-linux-x86_64.tar.gz \
    --repo nimesh08/quantum-stack

# Without gh, the install script falls back to the published
# checksums.txt next to the artifact.
```

The attestation chain ends at GitHub's OIDC issuer; no human-held
key material is in the loop. See the [Sigstore docs](https://docs.sigstore.dev/cosign/verifying/verify/)
for the full trust model.

---

## Quick smoke test after install

```bash
# Source the helper env so the C++ binaries find the python helper.
. ~/.local/heisenberg/env.sh

photonc version
photonc targets | head
photonc providers

# Hello, world: a Bell pair on IBM Heron r2 in cassette mode (no token needed).
cat > /tmp/bell.pho <<'EOF'
target ibm_heron_r2

kernel bell() -> int {
  QReg q(2)
  q.h(0)
  q.cx(0, 1)
  return q.measure_int()
}
EOF

photonc run /tmp/bell.pho \
    --target ibm_heron_r2 \
    --shots 1000 \
    --mode cassette
```

Expected output:

```
shots=1000 total counts=1000
histogram:
  |00>:   498  ###################
  |11>:   502  ####################
```

---

## Submitting to real IBM Quantum hardware

```bash
# Save your token to a file the install script's standard recommends:
mkdir -p ~/.config/heisenberg
echo "<paste-your-IBM-Quantum-token>" > ~/.config/heisenberg/ibm.token
chmod 600 ~/.config/heisenberg/ibm.token

photonc run /tmp/bell.pho \
    --target ibm_heron_r2 \
    --provider ibm \
    --shots 1024 \
    --mode live \
    --api-key-file ~/.config/heisenberg/ibm.token
```

The submission goes verbatim (Rule 5) through
`qiskit_ibm_runtime.SamplerV2(skip_transpilation=True)`; no
re-transpilation on the provider side.

---

## Build-from-source alternative

If you'd rather build the binaries yourself:

```bash
git clone https://github.com/nimesh08/quantum-stack.git ~/quantum-stack
cd ~/quantum-stack
cmake -S . -B build -G Ninja
ninja -C build spinorc photonc phononc qs_common_cli
pip install -e spinor/submit/python

# Helper env so the binaries find each other:
export QSTACK_SPINORC=$(pwd)/build/spinor/cli/spinorc
export QSTACK_PYTHONPATH=$(pwd)/spinor/submit/python
export SPINOR_REGISTRY_ROOT=$(pwd)/spinor/registry
```

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `provenance verification FAILED` | Either re-run with `gh auth login`, or `--no-verify` (only if you trust the source) |
| `python3: No module named spinor_submit` | `export QSTACK_PYTHONPATH=~/.local/heisenberg/python`, or source `~/.local/heisenberg/env.sh` |
| `unknown chip id: ...` | Run `photonc targets` to see the 27 chips the registry knows about, or update `SPINOR_REGISTRY_ROOT` |
| `secret-bearing flag must use --api-key-file` | You passed `--api-key=<literal>`; use `--api-key-file <path>` instead. Open-cli-collective rule. |
| Nothing prints when running cassette mode | Check that `--program-name` matches a recorded JSON under `python/spinor_submit/cassettes/` |

---

## Uninstall

```bash
rm -rf ~/.local/heisenberg ~/.local/bin/{spinorc,photonc,phononc,spinor-submit}
```
