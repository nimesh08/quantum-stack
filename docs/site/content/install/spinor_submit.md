# `spinor_submit` package

The Phase A submission adapter. One uniform `submit()` function over
IBM, AWS Braket, Azure Quantum, and a local simulator. Always
**verbatim** mode (Rule 5).

## Install

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install -e spinor/submit/python
```

## Smoke test

```python
import spinor_submit

# Cassette mode (default) — replays recorded JSON.
hist = spinor_submit.submit(
    "OPENQASM 3.0;\nbit[2] c;\nh $0; cx $0, $1; c[0]=measure $0; c[1]=measure $1;\n",
    chip="ibm_heron_r2",
    provider="ibm",
    shots=1000,
    program_name="bell",
)
print(hist.counts)   # {'00': 498, '11': 502}
```

## Modes (`SPINOR_SUBMIT_MODE`)

| Mode | Behaviour | Network? | Credentials? |
|---|---|---|---|
| `cassette` (default) | replays recorded JSON in `spinor_submit/cassettes/<provider>/<name>.json` | no | no |
| `live` | hits the real provider | yes | yes |
| `local` | runs the C++ `LocalProvider` simulator via `spinorc` | no | no |

For live mode set `SPINOR_SUBMIT_MODE=live` and provide credentials
per provider (see the package's [credentials guide](../packages/spinor_submit/credentials.md)).

## Public surface

- `spinor_submit.submit(qasm_text, chip, *, provider, shots, program_name)`
  → `Histogram`
- `spinor_submit.Histogram(counts: dict[str, int], shots: int)`
- `spinor_submit.Job(id, status, provider)`
- `spinor_submit.SUPPORTED_PROVIDERS = ('ibm', 'aws', 'azure', 'local')`

## See also

- [`spinor_submit` package reference](../packages/spinor_submit/reference.md)
- [Recording cassettes](../packages/spinor_submit/recording_cassettes.md)
- [Provider credentials](../packages/spinor_submit/credentials.md)
- [`spinor_submit` cookbook](../packages/spinor_submit/cookbook.md)
