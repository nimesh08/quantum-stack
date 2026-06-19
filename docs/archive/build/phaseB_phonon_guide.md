# Phase B — Phonon User Guide

End-of-phase user-facing guide. The document a person new to
the project reads to understand and use Phonon.

For per-milestone engineering specs see
[`phaseB/`](phaseB/README.md). For the build journal see
[`phaseB_progress.md`](phaseB_progress.md). For deviations from
the Phonon Engineering Deep-Dive see
[`phaseB_decisions.md`](phaseB_decisions.md).

---

## Three on-ramps

Pick yours:

- **From quantum (Qiskit / Braket / Cirq).** Skip §1. Read §2
  (the language) and §3 (linear types and what they prevent).
- **From compilers (LLVM / MLIR / language design).** Skip §2.
  Read §1 (a one-page quantum primer) and then §3.
- **From neither.** Read §1 → §2 → §3 in order.

The full glossary lives at
[`phaseB/glossary.md`](phaseB/glossary.md).

## §1 — Quantum primer (1 page)

> Skip if you have used Qiskit, Braket, or Cirq.

A **qubit** holds a blend of 0 and 1 simultaneously
(superposition). A **gate** is one quantum instruction:
`h q[0]`, `cx q[0], q[1]`, `rz(angle) q[0]`. A **circuit** is a
flat list of gate applications, ending with **measurements**
that collapse each qubit to a classical **bit**. Because
measurement is probabilistic, programs run many **shots** and
the answer is a histogram over outcomes.

Two physical laws Phonon makes the compiler honour:

- **No-cloning.** No physical operation can copy an unknown
  qubit. Phonon makes attempts to do so a *compile error*.
- **Once you measure, the qubit is destroyed.** Subsequent
  use is illegal unless the chip supports `reset` (re-prep)
  or **mid-circuit measurement** (a registry capability flag).

That's all the quantum you need.

## §2 — The Phonon language (1 page)

A Phonon source file (`.phn`) is a *strict superset* of a
Spinor source file (`.spn`). Every legal `.spn` is a legal
`.phn` character-for-character; you can rename `.spn` → `.phn`
and the program still compiles.

What Phonon adds on top:

- **Functions.** `def name(qubit a, qubit b) { ... }` then
  `name(q[0], q[1])`.
- **Counted loops.** `for i in 0..N { ... }` (N is a
  compile-time integer).
- **Conditionals.** `if (c[0] == 1) { ... } else { ... }`.
- **Classical scalars.** `int n = 5`; `angle theta = pi/4`.

Putting it together — the canonical "function + loop"
example:

```
target generic
qubit q[4]
bit c[4]

def bell_pair(qubit a, qubit b) {
  h a
  cx a, b
}

bell_pair(q[0], q[1])
bell_pair(q[2], q[3])
c = measure q
```

That program builds two Bell states on a 4-qubit register and
measures all four qubits. It is parsed by Phonon, type-checked
(below), optimized, lowered to flat Spinor, then handed to
Phase A's `spinorc` for placement / routing / decomposition /
emission.

## §3 — The linear type system

A regular type system catches `1 + "hello"` at compile time.
Phonon's type system does the same for cloning a qubit:

```
qubit q[1]
h q[0]    // OK
h q[0]    // ALSO OK in the surface syntax (the parser threads
          // SSA values through the slot, so this is "h on the
          // post-first-h q[0]"). Compile-time cloning is a
          // detectable error only if you build the IR by hand
          // and forget to thread.
```

What the checker DOES catch — built straight against the
SSA value-semantics:

- **E1 (no-cloning).** A qubit value is used as an input to
  more than one gate.
- **E2 (use-after-measure).** A qubit value is used after
  measure, without an intervening `reset` (and the chip's
  registry says it cannot do mid-circuit measurement-based
  feed-forward).
- **Implicit discard (warning).** A qubit is allocated but
  never measured / reset / consumed. (Like Rust's
  unused-variable warning, this is a warning, not an error.)

For the strategic case behind this design see Phonon
Deep-Dive Part 2 §5.4.

## §4 — The optimizer

This is THE optimizer for the whole stack. It runs once, on
the Phonon dialect, **before** lowering to Spinor and **above**
all chips. Spinor never optimises (Rule 2).

Pipeline order:

```
Phonon module
  ├── linear-type-check                   (M3)
  ├── built: cancellation                 (M5)
  ├── built: rotation merging             (M5)
  ├── borrowed: synthesis (oracle.* calls)(M6 — tweedledum)
  ├── borrowed: ZX simplify               (M6 — PyZX / quizx)
  ├── built: cancellation again
  ├── built: rotation merging again
  ├── built: scheduling (depth report)    (M5)
  └── lower → Spinor (flat)               (M4)
```

Then Phase A's pipeline takes over (placement → routing →
decomposition → emit → submit).

### Built passes

Implemented end-to-end in `phonon/optimizer/lib/Optimizer.cpp`:

- **Gate cancellation.** `H H` → empty; `X X` → empty;
  `S Sdg` → empty; `T Tdg` → empty; `Sx Sxdg` → empty;
  `CX CX` (same control/target) → empty; `CZ CZ` → empty;
  `Swap Swap` → empty.
- **Rotation merging.** `Rz(a); Rz(b)` → `Rz(a+b)` modulo 2π;
  zero-fold drops zero rotations.
- **Commutation reorder.** Re-runs cancellation + merging
  after one cycle; this catches patterns the first cycle
  exposes.
- **Scheduling.** ASAP depth report; the actual op order in
  the module is unchanged (Phase A's emitter handles
  chip-specific scheduling).

### Borrowed adapters

Owned C++ interfaces (`ISynthesizer`, `IZxSimplifier`) with two
implementations each:

- **NullImpl** — passthrough, used by default in CI.
- **TweedledumSynthesizer** — when `SPINOR_HAVE_TWEEDLEDUM` is
  defined and tweedledum 1.1.1 (MIT, C++17,
  github.com/boschmitt/tweedledum) is on the include path.
- **PyZXSubprocessSimplifier** — looks for a JSON cassette
  under `${PHONON_CASSETTE_DIR}/zx/<module-name>.json` and
  replays it. Live PyZX 0.10.3 (Apache-2.0, zxcalc/pyzx)
  invocation is gated by `PHONON_PYZX_LIVE=1`. The Rust port
  **quizx** 0.3.0 is documented as the future replacement
  (Decision D12).

The interface design is the swap-policy story: every borrowed
piece can be replaced without touching anything else.

## §5 — Installing & running

Phonon builds as part of the same CMake project as Spinor.
There is no separate `phononc` CLI yet — Phase B uses Phase
A's `spinorc` after a programmatic optimize+lower. A small
example program:

```cpp
#include "phonon/parser/Parser.h"
#include "phonon/types/LinearTypeChecker.h"
#include "phonon/optimizer/Pipeline.h"
#include "phonon/lower/Lowering.h"

auto pr = phonon::parser::parse(read_file("foo.phn"));
phonon::types::Options o;
phonon::dialect::Diagnostics d;
phonon::types::typecheck(*pr.module, o, d);

phonon::optimizer::PipelineConfig cfg;
cfg.zx = phonon::optimizer::makePyZXSimplifier();
phonon::optimizer::runPipeline(*pr.module, std::move(cfg));

auto target_info = ...;  // from Phase A registry
auto lr = phonon::lower::lower(*pr.module, &target_info);
// lr.module is flat Spinor; pass to Phase A's compile / emit / submit.
```

A user-facing `phononc` CLI is the natural Phase C work
when Photon brings frontends.

## §6 — Pinned versions

Re-verified upstream **2026-06-16**.

| Component | Pin | Source |
| --- | --- | --- |
| LLVM / MLIR | 22.1.7 | github.com/llvm/llvm-project |
| C++ | C++20 | — |
| Eigen | 5.0.1 | gitlab.com/libeigen/eigen |
| **tweedledum** | **1.1.1** (MIT, C++17, last tag 2021-09-08) | github.com/boschmitt/tweedledum |
| **PyZX** | **0.10.3** (Apache-2.0, 2026-06-01) | github.com/zxcalc/pyzx |
| **quizx** | **0.3.0** (Apache-2.0, Rust, 2025-06-12) | github.com/zxcalc/quizx |
| **pytket (TKET)** | **2.16.0** (Apache-2.0, 2026-03-25) — benchmark only | github.com/CQCL/tket |

Decisions D9–D12 in
[`phaseB_decisions.md`](phaseB_decisions.md) explain anything
that deviates from the Deep-Dive.

## §7 — Where to put new code

| You're adding… | Folder |
| --- | --- |
| A control-flow op | `phonon/dialect/` |
| A grammar rule | `phonon/parser/cpp/` |
| A linear-type rule | `phonon/types/` |
| A lowering pattern | `phonon/lower/` |
| A built optimizer pass | `phonon/optimizer/` |
| A borrowed-pass adapter | `phonon/optimizer/` (behind `BorrowedPasses.h`) |
| A cassette | `phonon/optimizer/cassettes/zx/` |
| A test | `phonon/tests/<m1..m7>/` |
| A benchmark | `phonon/bench/` |
| Anything in Spinor or Photon | **Stop.** Wrong phase. |

## §8 — Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `error: E1: qubit value %v used more than once` | You consumed the same qubit twice as input — the no-cloning theorem caught at compile time. Thread the qubit through the second op's result instead of reusing the original value. |
| `error: E2: qubit %v used after measurement; insert 'reset' first` | A gate appears after a measure on the same qubit. Insert a `reset` (re-preps the qubit in |0⟩) before the gate, or pick a chip with mid-circuit measurement. |
| `error: for-loop bounds must be compile-time integers` | `for i in lo..hi` — both bounds must fold to integer literals at parse time. Replace `hi` with a literal or with an `int` declared earlier. |
| `error: recursive call to '<f>' is not supported` | Phase B forbids recursive functions; rewrite as a counted loop. |
| `warning: qubit %v is implicitly discarded` | You allocated a qubit and never used it. The pipeline still runs; this is a hint that the program has a typo. |
| `lowering: phonon.while is not supported` | Replace with a counted `for` loop. |

## §9 — Definition of Done (for Phase B)

All boxes ticked at end-of-phase:

- [x] A structured Phonon program (incl. teleportation with
      feed-forward) type-checks, optimizes once, lowers to
      Spinor, and runs through the Phase A pipeline.
- [x] The linear type checker rejects cloning and
      use-after-measure with precise errors (E1, E2).
- [x] Every Spinor program is accepted unchanged as Phonon
      (containment holds — verified by the M2 corpus test
      `M2_contain.*`).
- [x] Borrowed optimizer pieces sit behind interfaces and
      are swappable (`M7_swap.null_vs_pyzx_same_corpus`);
      Qiskit is **not** in the optimize path
      (compile-time true: the optimizer library doesn't link
      against qiskit at all).
- [x] All Phase B milestone tests pass in CI:
      **17 phonon ctest entries** (4 × M1, 3 × M2, 2 × M3,
      2 × M4, 3 × M5, 1 × M6, 2 × M7) on top of Phase A's
      15 ctest entries.
- [x] `phaseB_progress.md`, `phaseB_decisions.md`, and this
      guide are written.
- [x] **Stop.** Do **not** start Photon (Phase C). The next
      phase is a fresh agent conversation per the handoff
      brief.

## §10 — Glossary

See [`phaseB/glossary.md`](phaseB/glossary.md). If you find a
term not listed there, it is a documentation bug — please file
or fix.
