# Recording cassettes

How to add a new replay-mode cassette so future tests don't need
network or credentials.

## 1. Submit live, capture the result

```python
import os, json
os.environ["SPINOR_SUBMIT_MODE"] = "live"
os.environ["IBM_QUANTUM_TOKEN"] = "..."

import spinor_submit
hist = spinor_submit.submit(qasm, "ibm_heron_r2",
                            provider="ibm", shots=1000)

cassette = {
    "_recorded_against": "ibm_heron_r2 (live)",
    "_program": "your_program.spn",
    "_shots": 1000,
    "counts": hist.counts,
}
with open("spinor/submit/python/spinor_submit/cassettes/ibm/your_program.json", "w") as f:
    json.dump(cassette, f, indent=2)
```

## 2. Verify by replaying

```python
os.environ["SPINOR_SUBMIT_MODE"] = "cassette"
hist = spinor_submit.submit(qasm, "ibm_heron_r2",
                            provider="ibm", shots=1000,
                            program_name="your_program")
print(hist.counts)
```

The cassette filename pattern is
`cassettes/<provider>/<program_name>.json`. Match the
`program_name` in `submit()`.

## 3. Commit

Cassettes are tracked in git so PR builds work without credentials.
Inspect existing ones at
[`spinor/submit/python/spinor_submit/cassettes/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/submit/python/spinor_submit/cassettes).

## See also

[Modes](modes.md), [Credentials](credentials.md)
