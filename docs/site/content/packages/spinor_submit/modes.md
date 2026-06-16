# Modes — `cassette` / `live` / `local`

`SPINOR_SUBMIT_MODE` controls how `submit()` reaches the provider.

## `cassette` (default)

Replays recorded JSON from
`spinor_submit/cassettes/<provider>/<program_name>.json`. No
network, no credentials. Used in CI and local dev.

```python
import os
os.environ["SPINOR_SUBMIT_MODE"] = "cassette"
hist = spinor_submit.submit(qasm, "ibm_heron_r2",
                            provider="ibm", shots=1000,
                            program_name="bell")
```

If the cassette is missing:

```
FileNotFoundError: no cassette for ibm/bell at spinor/submit/python/
                   spinor_submit/cassettes/ibm/bell.json
```

Record one with [Recording cassettes](recording_cassettes.md).

## `live`

Hits the real provider. **Requires credentials** — see
[Credentials](credentials.md). Submits in **verbatim** mode (no
provider-side transpilation).

```bash
export SPINOR_SUBMIT_MODE=live
export IBM_QUANTUM_TOKEN=...
```

## `local`

Runs the C++ `LocalProvider` (statevector simulator) via the
`spinorc` binary. No network. Useful when you want a quick "would
this really run?" check without setting up cassettes.

```bash
export SPINOR_SUBMIT_MODE=local
export SPINORC_BIN=/opt/quantum-stack/build/spinor/cli/spinorc
```

The Python adapter's `local` mode currently just shells to spinorc
and reports zero counts (the C++ LocalProvider's sampling is
covered by `spinor_m10_test`).

## See also

[Credentials](credentials.md), [Recording cassettes](recording_cassettes.md)
