# Install — `spinor_submit`

The Phase A submission adapter package.

## Quickstart

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
python3.12 -m venv .venv && source .venv/bin/activate
pip install -e spinor/submit/python
```

## Smoke test

```python
import spinor_submit

hist = spinor_submit.submit(
    "OPENQASM 3.0;\nbit[2] c;\nh $0; cx $0, $1; c[0]=measure $0; c[1]=measure $1;\n",
    chip="ibm_heron_r2",
    provider="ibm",
    shots=1000,
    program_name="bell",
)
print(hist.counts)
```

(Default `SPINOR_SUBMIT_MODE=cassette` replays the recorded JSON at
`spinor/submit/python/spinor_submit/cassettes/ibm/bell.json`.)

## See also

- [Reference](reference.md), [Modes](modes.md), [Credentials](credentials.md)
- [Recording cassettes](recording_cassettes.md), [Cookbook](cookbook.md)
