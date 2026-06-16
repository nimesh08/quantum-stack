# Cookbook — `spinor_submit`

## 1. Cassette mode (most common)

```python
import os
os.environ["SPINOR_SUBMIT_MODE"] = "cassette"

import spinor_submit
hist = spinor_submit.submit(open("bell.qasm").read(), "ibm_heron_r2",
                            provider="ibm", shots=1000,
                            program_name="bell")
print(hist.counts)
```

## 2. Live IBM submission

```python
os.environ["SPINOR_SUBMIT_MODE"] = "live"
os.environ["IBM_QUANTUM_TOKEN"] = "..."
hist = spinor_submit.submit(qasm, "ibm_heron_r2",
                            provider="ibm", shots=1000)
```

## 3. Live Braket submission

```python
os.environ["SPINOR_SUBMIT_MODE"] = "live"
# AWS creds in ~/.aws/credentials
hist = spinor_submit.submit(qasm, "ionq_forte",
                            provider="aws", shots=1000)
```

## 4. Local simulator (C++ via spinorc)

```python
os.environ["SPINOR_SUBMIT_MODE"] = "local"
os.environ["SPINORC_BIN"] = "/opt/quantum-stack/build/spinor/cli/spinorc"
hist = spinor_submit.submit(qasm, "ionq_aria_proto",
                            provider="local", shots=1000)
```

## 5. Driving from `jobsvc`

The Phase D worker uses `spinor_submit.submit` automatically — see
[`jobsvc.providers.submit_to_provider`](../../api/python/jobsvc/providers.md#jobsvc.providers.submit_to_provider).

## See also

[Reference](reference.md), [Modes](modes.md), [Credentials](credentials.md)
