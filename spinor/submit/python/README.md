# heisenberg-spinor-submit

The qstack runtime authenticates provider connections, discovers target
capabilities, compiles through the native Photon/Phonon/Spinor tools, and
executes saved artifacts. Provider SDKs handle transport; the owned C++
compiler produces the native circuit.

## Install

```console
python -m pip install heisenberg-photon
```

This installs the native compiler wheel and its matching runtime dependency.
For a separately installed C++ toolchain, install only the runtime:

```console
python -m pip install heisenberg-spinor-submit
```

The runtime wheel includes all 31 historical target profiles, their topology
data, and cassette fixtures. Compilation also requires the native compiler
binaries on PATH, or explicit QSTACK_SPINORC, QSTACK_PHONONC and QSTACK_PHOTONC
paths. Historical profiles are suitable for offline compilation; live runs
use verified provider discovery snapshots.

## Compile and run

```console
qstack compile program.pho --target ibm_heron_r2 --output program.qstack -O 2
qstack run program.pho --target ibm_heron_r2 --mode local --shots 1024
qstack providers --json
```

Local runs use the C++ simulator. Live runs require an explicit mode, route,
credentials, and current target capabilities. Install only the provider extra
you use, for example `heisenberg-spinor-submit[ibm]`. SDKs with incompatible
dependencies can use separate environments and the configured SDK Python path.

```python
import qstack

result = qstack.run_source(
    source, language="phonon", target="ibm_heron_r2",
    mode="local", shots=1024,
)
print(result.counts)
```

Use `qstack --help` and the subcommand help for authentication, configuration,
selectable environment files, saved artifacts, and job status/results.
[Provider contracts](qstack/providers/CONTRACTS.md) describe each route's
actual SDK/wire format and restrictions.

The legacy `spinor_submit` import and `python -m spinor_submit` entry point
remain available for compatibility. Cassette mode replays explicitly labeled
fixtures; it does not execute a new circuit.

## Development checks

```console
python -m pytest tests
python -m build
python tests/check_distribution.py dist/heisenberg_spinor_submit-0.6.0-py3-none-any.whl
```

The distribution check installs into a fresh environment outside the checkout.
It verifies registry and topology data, provider imports and the qstack command.
It does not submit provider jobs.
