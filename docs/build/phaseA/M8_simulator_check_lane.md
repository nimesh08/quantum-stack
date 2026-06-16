# M8 — Simulator + Check Lane

## 1. Goal & spec section

The simulator + equivalence checker + resource estimator that
runs after compilation and before any paid hardware run.
Implements [Spinor Engineering Deep-Dive][deep-dive] Part 3 §3.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

Decision D7 ([phaseA_decisions.md](../phaseA_decisions.md))
defers the Stim wrapper; M8 ships an in-tree dense statevector
engine that handles the corpus.

## 2. Inputs / outputs

- **Consumes:**
  - A `dialect::Module` (any contract; verifier-clean).
  - Optionally, a `registry::ChipInfo` for resource estimates.
- **Produces:**
  - `StateVector` — the post-execution complex amplitudes.
  - `EquivResult` — equivalent up-to-global-phase verdict.
  - `ResourceEstimate` — gate count, depth, qubits,
    measurements, error estimate, cost.

## 3. Deliverables

- `spinor/sim/include/spinor/sim/Simulator.h` — public API.
- `spinor/sim/lib/Simulator.cpp` — implementation.
- `spinor/sim/CMakeLists.txt`.
- `spinor/tests/m8/m8_test.cpp` + `CMakeLists.txt`.

## 4. Data structures

```c++
struct StateVector { std::size_t qubits; std::vector<cdbl> amps; };

struct EquivResult {
  bool equivalent;
  double maxAbsDiff;        // post-phase
  std::optional<cdbl> phase;
};

struct ResourceEstimate {
  std::size_t totalGates, twoQubitGates, depth, qubits, measurements;
  std::optional<double> totalErrorEstimate;
  std::optional<double> shotCostUsd;
};
```

## 5. Algorithms

### Statevector simulator

Dense `vector<complex<double>>` of length `2^n`. For each gate:

- **Single-qubit gate.** Apply 2×2 unitary by visiting every
  pair `(i, i|mask)` where bit `q` of `i` is 0. ~`2 · 2^n`
  complex multiplies per gate.
- **Two-qubit gate.** Apply 4×4 unitary by visiting every
  index where bits `qa` and `qb` are both 0, gathering the
  four amplitudes, and applying `U`. ~`8 · 2^n` complex
  multiplies per gate.

Cap at 24 qubits (refusing larger N at the API surface).

### Equivalence (up to global phase)

1. `simulate(a)` → `sa`; `simulate(b)` → `sb`.
2. If `qubits(a) != qubits(b)`: not equivalent.
3. Find the first index `i` with `|sb[i]| > 1e-7`; compute
   `phase = sa[i] / sb[i]`.
4. Reject if `||phase| - 1| > tol`.
5. Otherwise `maxAbsDiff = max_i |sa[i] - phase * sb[i]|`;
   equivalent iff `maxAbsDiff ≤ tol`.

### Resource estimator

Single forward pass with a per-physical-qubit depth tracker:

- Each gate increments `totalGates`; two-qubit ops also
  bump `twoQubitGates`.
- Depth update: `depth_after = max(depth_at(qubits)) + 1`;
  push `depth_after` to every operand qubit.
- Track measurements separately.
- With a `ChipInfo`: error model is `1 - (1-e2q)^N2 ·
  (1-e1q)^N1` with `e2q=0.01`, `e1q=0.001` baseline.
  Calibration-driven fidelities refine in the platform layer.

## 6. Test matrix

| ID | Name | Type | Inputs | Expected | CI gate |
| -- | ---- | ---- | ------ | -------- | ------- |
| M8-T01 | `M8_sim.bell_state_has_two_peaks` | unit | bell.spn | |00>+|11>, 50/50 | `ctest -L M8` |
| M8-T02 | `M8_sim.ghz_state_has_two_peaks` | unit | ghz.spn | |000>+|111>, 50/50 | `ctest -L M8` |
| M8-T03 | `M8_sim.rotations_unitary_norm` | property | rotations.spn | Σ\|a_i\|² = 1 | `ctest -L M8` |
| M8-T04 | `M8_equiv.route_preserves_meaning_bell` | integration | bell + 2q passthrough chip | equivalent up to phase | `ctest -L M8` |
| M8-T05 | `M8_equiv.deliberate_break_caught` | regression | H+Z vs H+X | flagged inequivalent | `ctest -L M8` |
| M8-T06 | `M8_resource.bell_counts` | unit | bell.spn | gates=2, depth=2, qubits=2, measures=2 | `ctest -L M8` |
| M8-T07 | `M8_resource.with_chip_fills_cost_and_error` | unit | bell + chip | cost > 0, error populated | `ctest -L M8` |
| M8-T08 | `M8_resource.full_corpus_loads` | integration | corpus | every file estimates cleanly | `ctest -L M8` |

## 7. Cookbook example

```c++
auto r = parser::parse(slurp("bell.spn"));
sim::StateVector sv = sim::simulate(*r.module);
// sv.qubits == 2;
// |sv.amps[0]|^2 == 0.5  (|00>)
// |sv.amps[3]|^2 == 0.5  (|11>)
// |sv.amps[1]| ≈ |sv.amps[2]| ≈ 0
```

End-to-end check via `spinorc check`:

```
$ spinorc check -t ionq_aria_proto bell.spn
equivalence: skipped (chip has 25 qubits; statevector capped at 12)
gates total: 10, twoQubit: 1, depth: 9, qubits: 25, measurements: 2
error (est.): 0.019, cost @1k shots: $10
```

## 8. Pitfalls

- **State-vector growth.** 2^25 complex doubles = 1 GB. The
  cap at 24 prevents accidental OOM.
- **Native 1q gate physics.** The baseline simulator treats
  `gpi`, `gpi2`, `u1q` as identity (Decision D6 + D7); the
  check-lane equivalence bar is "standard-gate equivalence
  on routed circuits whose decomposition uses standard-set
  passthrough". A per-chip simulator backend belongs to the
  platform layer.
- **Measurement and reset.** For equivalence checks we treat
  `measure` and `reset` as identity (we compare pre-measure
  states). M8-T04 captures this.
- **Phase finder edge case.** If `b`'s state is all-zero we
  treat the modules as equivalent only if `a` is all-zero
  too.

## 9. Definition of Done

- [x] Spec landed (this file).
- [x] All M8-T## tests green.
- [x] `spinorc check` wires up the check lane end-to-end.
- [x] Stim wrapper deferred per D7 (header surface only).

## 10. Open questions

None blocking. Stim integration tracked as a Phase D follow-up.
