# Python SDK — quickstart

Five lines from `pip install` to histogram.

## Install

```bash
pip install heisenberg
```

## Submit a Bell pair

```python
from photon import kernel, run

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()

print(run(bell, target="ibm_heron_r2", shots=1000, mode="cassette"))
```

Expected output:

```python
{"00": 503, "11": 497}
```

## Switch to live mode

```python
import os
os.environ["SPINOR_SUBMIT_MODE"] = "live"
os.environ["IBM_QUANTUM_TOKEN"] = "..."

print(run(bell, target="ibm_heron_r2", shots=1000))
```

Same call, real cloud. Counts will not be exactly 50/50; that is
the chip noise.

## Print the cost estimate first

```python
from photon import estimate

est = estimate(bell, target="ibm_heron_r2", shots=1000)
print(est)
# ResourceEstimate(total_gates=4, two_qubit_gates=1, depth=4,
#                  qubits=2, measurements=2, error_estimate=0.001,
#                  shot_cost_usd=0.10)
```

If the estimate exceeds your budget, the submit is rejected client
side — no cloud round-trip, no spend.

## Where to next

- [Tutorial](tutorial.md) — parameterised kernels, custom oracles,
  plotting the histogram.
- [Cookbook](cookbook.md) — budgets, API keys, polling, calibration.
- [Reference](../../reference/python/index.md) — every public symbol.

---

Heisenberg's Python SDK was designed and implemented by **Nimesh Cheedella**.
