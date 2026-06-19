# M10 — Submission Adapters

## 1. Goal & spec section

The submission layer that takes an emitted artifact and runs
it on a real provider, retrieving the measurement histogram.
Implements [Spinor Engineering Deep-Dive][deep-dive] Part 3 §5.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

Decision D8 (in [phaseA_decisions.md](../phaseA_decisions.md))
records the C++/Python split: the `IProvider` contract is C++,
the live provider implementations are Python (where the SDKs
are best-supported), and CI runs against recorded cassettes so
no tokens are required offline.

Every adapter submits in **verbatim / pass-through mode**
(Rule 5 — the result quality must be ours).

## 2. Inputs / outputs

- **Consumes:**
  - C++ side: a `dialect::Module` + `registry::ChipInfo` +
    `shots`.
  - Python side: an emitted QASM3 string + chip id + shots.
- **Produces:** a `Histogram` (bitstring → count).
- **Invariants:** every adapter implementation honours Rule 5
  (provider's compiler is disabled / circuits are submitted
  verbatim).

## 3. Deliverables

### C++

- `spinor/submit/include/spinor/submit/Provider.h` — `IProvider`,
  `LocalProvider`, `Job`, `Histogram`.
- `spinor/submit/lib/LocalProvider.cpp` — runs M8 simulator
  and samples shots.
- `spinor/submit/CMakeLists.txt`.

### Python

- `spinor/submit/python/spinor_submit/__init__.py` —
  `submit(qasm_text, chip, *, provider, shots, program_name)`.
- `spinor/submit/python/spinor_submit/cassettes/<provider>/<program>.json`
  — recorded histograms.
- `spinor/submit/python/pyproject.toml` — extras pin
  `qiskit-ibm-runtime`, `amazon-braket-sdk`, `azure-quantum`.
- `spinor/submit/python/tests/test_submit.py`.

### CLI

- `spinorc check` already wires the `LocalProvider`-backed
  resource estimator + equivalence check.

## 4. Data structures

### C++

```c++
struct Job        { std::string id, status, provider; };
struct Histogram  { std::map<std::string, std::size_t> counts;
                    std::size_t shots; };

class IProvider {
 public:
  virtual std::string name() const = 0;
  virtual Job submit(const Module&, const ChipInfo&, std::size_t) = 0;
  virtual Job poll(const std::string& jobId) = 0;
  virtual Histogram results(const std::string& jobId) = 0;
};

class LocalProvider : public IProvider { ... };  // M8-backed
```

### Python

```python
@dataclass
class Histogram:
    counts: dict[str, int]
    shots: int
```

## 5. Algorithms

### LocalProvider (C++)

1. `simulate(m)` → state vector.
2. Compute per-basis-state probabilities = `|amp|²`,
   normalise.
3. Sample `shots` outcomes via inverse-CDF sampling.
4. Bin into a `Histogram`.

### Cassette-mode submission (Python)

```python
mode = os.environ.get("SPINOR_SUBMIT_MODE", "cassette")
if mode == "cassette":
    return load_cassette(provider, program_name)
elif mode == "live":
    return live_submit(provider, qasm_text, chip, shots)
```

Each provider's live shim:

- **IBM** — `qiskit-ibm-runtime`'s `SamplerV2` with
  `skip_transpilation=True` (verbatim).
- **AWS** — `braket.aws.AwsDevice` + `OQ3Program(source=...)`.
  Braket reads the `#pragma braket verbatim` annotation in
  the QASM — that's where Rule 5 lives.
- **Azure** — `azure.quantum.Workspace.get_targets(...).submit(
  input_data_format="openqasm-v3", ...)`. Azure's verbatim is
  per-target; Quantinuum honours the QIR Adaptive profile
  pragma.

## 6. Test matrix

| ID | Name | Type | Inputs | Expected | CI gate |
| -- | ---- | ---- | ------ | -------- | ------- |
| M10-T01 | `M10_local.bell_returns_two_peaks` | integration | bell.spn, 2000 shots | only 00, 11 in histogram, ~50/50 | `ctest -L M10` |
| M10-T02 | `M10_local.ghz_returns_two_peaks` | integration | ghz.spn | 000, 111 dominate | `ctest -L M10` |
| M10-T03 | `M10_local.poll_unknown_returns_error` | unit | unknown job id | status="error" | `ctest -L M10` |
| M10-T04 | `M10_local.name_is_local` | unit | provider.name() | "local" | `ctest -L M10` |
| M10-T05 | `python.test_cassette_returns_bell_histogram[ibm]` | integration | IBM cassette | ~50/50 | `ctest -L M10 -L python` |
| M10-T06 | `python.test_cassette_returns_bell_histogram[aws]` | integration | AWS cassette | ~50/50 | `ctest -L M10 -L python` |
| M10-T07 | `python.test_cassette_returns_bell_histogram[azure]` | integration | Azure cassette | ~50/50 | `ctest -L M10 -L python` |
| M10-T08 | `python.test_unknown_provider_raises` | unit | provider="not_real" | ValueError | `ctest -L M10 -L python` |
| M10-T09 | `python.test_supported_providers_list` | unit | SUPPORTED_PROVIDERS | ibm/aws/azure/local | `ctest -L M10 -L python` |
| M10-T10 | `python.test_cassette_missing_raises` | regression | unknown program_name | FileNotFoundError | `ctest -L M10 -L python` |
| M10-T11 | `python.test_live_ibm_smoke` | live | IBM token | round-trips a real submission | `SPINOR_LIVE_IBM_TEST=1` |

## 7. Cookbook example

### Offline (cassette mode, default)

```python
from spinor_submit import submit

with open("bell.qasm") as f:
    qasm = f.read()

hist = submit(qasm, chip="ibm_heron_r2",
              provider="ibm", shots=1000, program_name="bell")
print(hist.counts)
# {'00': 498, '11': 502}
```

### Live (with credentials)

```bash
$ export SPINOR_SUBMIT_MODE=live
$ export QISKIT_IBM_TOKEN=...
$ python -c "from spinor_submit import submit; \
            hist = submit(open('bell.qasm').read(), \
                          chip='ibm_heron_r2', provider='ibm', \
                          shots=1000); \
            print(hist.counts)"
```

### C++ end-to-end

```c++
spinor::submit::LocalProvider provider;
auto job = provider.submit(module, chip, /*shots=*/1000);
auto hist = provider.results(job.id);
// hist.counts == {"00": ~500, "11": ~500}
```

## 8. Pitfalls

- **Live tokens.** Never check tokens into the repo. The
  Python adapters read them from environment / SDK config
  per provider (Qiskit's runtime profile, AWS credentials,
  Azure CLI login).
- **Cassette drift.** Hardware drifts; cassettes record one
  point in time. Regenerate when behaviour changes; the
  recording helper is documented in
  `submit/python/README.md`.
- **Verbatim must be enabled.** Not the default for any
  provider. IBM: `skip_transpilation=True` on the sampler.
  AWS: pragma in the QASM. Azure: input format
  `openqasm-v3` + the right target. Without these, the
  provider re-optimises and Rule 5 is broken.
- **Histogram bit ordering.** Each provider has its own
  little/big-endian convention. We normalise to the same
  bitstring convention as the local sim (qubit 0 = leftmost
  bit) — adapters convert before returning.

## 9. Definition of Done

- [x] Spec landed (this file).
- [x] All M10-T## tests green (4 C++ + 6 Python; 1 live test
      gated).
- [x] Cassette mode keeps CI offline.
- [x] Verbatim mode honoured by every live adapter.

## 10. Open questions

When the user provides live IBM/Braket/Azure tokens, the
maintainer will record fresh cassettes and re-verify the
live tests against simulator-backed cloud endpoints (this is
the "user-supplied credentials" path called out in the plan's
§7 question 4). For now, recorded cassettes are the truth.
