# M7 — Pass orchestration + swap policy + benchmarks

## 1. Goal & spec section

Compose the optimize-once pipeline and prove the swap policy
works. Implements **Phonon Deep-Dive Part 3 §§6.2, 8** (the
governing principle and the swap policy), and the end-of-Phase-B
benchmark requirement.

The pipeline assembles M5 (built passes) + M6 (borrowed
adapters) into a single entry point that is THE optimizer for
the whole stack: it runs once on the Phonon dialect, above all
chips, before lowering to Spinor (M4) and Phase A's chip-specific
passes.

A Python harness measures gate-count delta vs. **pytket** (TKET
2.16.0 — Apache-2.0, Quantinuum/CQCL) and Qiskit's transpiler,
both run in their *own* compilation pipelines. Qiskit is **never**
invoked on our optimize path; it is a comparison baseline only.

## 2. Inputs / outputs

```cpp
namespace phonon::optimizer {

struct PipelineConfig {
  std::unique_ptr<ISynthesizer>  synth     = makeNullSynthesizer();
  std::unique_ptr<IZxSimplifier> zx        = makeNullZxSimplifier();
  bool runBuiltAfterBorrowed                = true;
};

Stats runPipeline(dialect::Module& m, PipelineConfig cfg = {});

}  // namespace phonon::optimizer
```

## 3. Deliverables

- `phonon/optimizer/include/phonon/optimizer/Pipeline.h`
- `phonon/optimizer/lib/Pipeline.cpp`
- Tests in `phonon/tests/m7/`:
  - **End-to-end.** For each Phonon corpus fixture, run
    parse → typecheck → optimize → lower → Phase A verify.
  - **Equivalence.** The lowered output simulates to the same
    state vector as the *un*optimized lowered output (M8 sim).
  - **Swap-policy.** Two pipeline runs with different `IZxSimplifier`
    impls produce identical gate counts on the cassette corpus
    (proves swap is observably equivalent).
- `phonon/bench/` — benchmark Python harness (separate from
  CI hot loop):
  - `phonon/bench/bench.py` — reads a flat-Spinor `.qasm`
    artifact emitted by `spinorc`, runs it through pytket and
    Qiskit transpilers, prints a markdown table comparing gate
    counts and depth.
  - `phonon/bench/README.md` — how to run the harness.

## 4. Data structures

The pipeline order:

```
Phonon module
  ├── linear-type-check (M3)               [error if cloning]
  ├── built: cancellation                  (M5)
  ├── built: rotation merging              (M5)
  ├── borrowed: synthesis (oracle.* calls) (M6)
  ├── borrowed: ZX simplify                (M6)
  ├── built: cancellation                  (re-run, expose new)
  ├── built: rotation merging              (re-run)
  ├── built: scheduling                    (M5)
  └── lower → Spinor                       (M4)
```

## 5. Algorithms

Sequential application; the only subtlety is that we run the
built passes a *second* time after the borrowed passes — the
borrowed passes can introduce new patterns that the built ones
can clean up.

## 6. Test matrix

| Test | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M7_e2e.bell_full_pipeline` | e2e | `bell.spn` parsed as Phonon | optimized + lowered passes Phase A verify, sim succeeds. |
| `M7_e2e.bell_pair_func_full` | e2e | `bell_pair_func.phn` | passes pipeline; lowered op count is reasonable. |
| `M7_e2e.qft_loop_full` | e2e | `qft_loop.phn` | passes pipeline. |
| `M7_e2e.teleportation_full` | e2e | `teleportation.phn` (target ibm_heron_r2) | passes pipeline. |
| `M7_swap.null_vs_pyzx_same_corpus` | property | bell module + each impl | gate-count outputs identical (Phase B identity-cassette property). |
| `M7_no_qiskit_in_optimize` | property | (compile-time) | the `phonon::optimizer` library does not link to qiskit (vacuous; documented). |

## 7. Cookbook example

```cpp
#include "phonon/parser/Parser.h"
#include "phonon/optimizer/Pipeline.h"
#include "phonon/lower/Lowering.h"

auto pr = phonon::parser::parse(read_file("foo.phn"));
phonon::optimizer::PipelineConfig cfg;
cfg.zx = phonon::optimizer::makePyZXSimplifier();
phonon::optimizer::runPipeline(*pr.module, std::move(cfg));
auto lr = phonon::lower::lower(*pr.module);
// lr.module is now flat Spinor, ready for Phase A.
```

## 8. Pitfalls

- **Pipeline reordering.** Running borrowed passes BEFORE the
  first cancellation cycle is wasteful (Tweedledum may emit
  redundant gates that built-in passes would otherwise catch
  upfront). Order matters; the spec encodes it.
- **Stats arithmetic.** Pipeline aggregates per-pass stats but
  some passes (re-run) can over-count; we report delta from
  initial gate count as the canonical metric.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M7`.
- [ ] Phase A tests still green.
- [ ] `phaseB_progress.md` entry appended; D13 (pipeline
      ordering) recorded if needed.
- [ ] `phaseB_phonon_guide.md` written (end-of-phase user-
      facing guide).
- [ ] `phase-b-ci.yml` GitHub Actions workflow shipped.

## 10. Open questions

None.
