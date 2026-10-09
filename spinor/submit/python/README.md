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

New compilations produce version-2 artifacts with logical and physical IR,
typed classical mappings, capability requirements and numerical diagnostics.
Version-1 artifacts keep their original hashes and execution semantics. Install
the `verify` extra and run `qstack verify program.qstack` for independent offline
operator/instrument comparisons. The command reports `passed`, `failed` or
`not_checked`, stores artifact-bound evidence, and never submits a job. Numerical
diagnostics and finite comparisons are not certified whole-program error bounds.

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
It checks the bundled registry, topology data, installed module origins and the
`qstack` command, then installs the optional `verify` extra. It exercises v1 hash
compatibility, v2 artifact sidecars, exact 64-bit classical values, full-phase
verification, a deliberately wrong gate angle, and the verifier's size limit.
CLI outcomes must distinguish passed, failed and not checked. Add
`--output package-evidence.json` to retain wheel hashes and check results.
Only dependency installation uses the network; no compiler or provider jobs run.
