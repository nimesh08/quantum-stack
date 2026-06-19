# Python SDK

Heisenberg's Python SDK is four PyPI packages that work together:

| Package | What it gives you |
|---------|-------------------|
| `heisenberg` | The launcher (`heisenberg run`) plus everything below as a transitive dependency. |
| `heisenberg-photon` | The `@kernel` decorator, `QReg`, `compile()`, `run()`, and the `photon.lib` standard library. |
| `heisenberg-spinor-submit` | Provider adapters (IBM, AWS Braket, Azure, QCI, Anyon, TII, Alice and Bob) with cassette mode. |
| `heisenberg-jobsvc` | The FastAPI service. Useful as a library if you want to mount the API inside another app. |
| `heisenberg-calibration` | The APScheduler nightly refresh job. |

For most users, `pip install heisenberg` is the only thing you ever
type — it pulls the others. Power users who do not want the
launcher can install the smaller pieces directly.

## What is in this section

- [Install](install.md) — `pip install` recipes; venv, Conda,
  poetry.
- [Quickstart](quickstart.md) — submit a Bell pair from a Python
  script in five lines.
- [Tutorial](tutorial.md) — write a parameterised kernel, compile
  it, run it, plot the histogram.
- [Cookbook](cookbook.md) — recipes for budgets, API keys, polling,
  cassette recording, custom calibration sources, etc.
- [Reference](../../reference/python/index.md) —
  auto-generated API docs (mkdocstrings).

## A complete example

```python
from photon import kernel, run

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()

print(run(bell, target="ibm_heron_r2", shots=1000, mode="cassette"))
# {"00": 503, "11": 497}
```

That's the whole flow: declare a kernel, call `run()`, get a
histogram. The compile pipeline (Photon → Phonon → Spinor → emit)
runs locally; submission goes through `spinor-submit` adapters.

## Through `jobsvc` instead of in-process

If you have `heisenberg run` already serving on `localhost:8080`,
you can submit through the HTTP API instead — useful when you want
audit logs, budgets, and shared queues:

```python
import httpx

r = httpx.post(
    "http://localhost:8080/api/v1/jobs",
    headers={"X-API-Key": "Q4r2p8aA....."},
    json={
        "source": "target generic\nqubit q[2]\nbit c[2]\nh q[0]\ncx q[0],q[1]\nc=measure q\n",
        "source_kind": "spinor",
        "target": "ibm_heron_r2",
        "shots": 1000,
    },
)
print(r.json()["estimate"])
```

Both paths land at the same compiler.

---

Heisenberg's Python SDK was designed and implemented by **Nimesh Cheedella**.
