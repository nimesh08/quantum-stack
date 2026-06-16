# Phase B (Phonon) — M0 overview

Phonon is the chip-independent middle layer of the
Photon · Phonon · Spinor stack. It does three things:

1. **Adds structure to Spinor.** Loops, ifs, functions,
   classical expressions — the constructs Spinor deliberately
   omitted. The Phonon grammar is a strict superset of Spinor's:
   every legal `.spn` file is a legal `.phn` file character for
   character.
2. **Enforces no-cloning at compile time.** The linear type
   system makes the `qubit` type used-exactly-once, so attempting
   to copy a qubit (use it twice as input) is a static error.
3. **Runs THE optimizer once, above all chips.** Gate
   cancellation, rotation merging, commutation reordering,
   classical-logic synthesis, ZX simplification, scheduling.
   This pass collection runs once on the Phonon dialect, then
   the program is lowered to flat Spinor and Phase A's
   placement/routing/decomposition specialise it to one chip.

```
.phn source
  → tokens (M2)
  → AST (M2)
  → Phonon IR  (M1 dialect, extends Spinor)
  → Linear type check (M3)            (no-cloning compile error)
  → OPTIMIZE ONCE                     (M5+M6)
  → lower (M4)                        (inline, unroll, flatten)
  → flat Spinor IR (Phase A)
  → Phase A: placement → routing → decomposition → emit → submit
```

## Three on-ramps

- **From quantum.** Skip §1; Phonon is what your circuit DSL
  compiles into. Read §2 (the additions: if/for/def + linear
  types) and §4 (what the optimizer guarantees).
- **From compilers.** Phonon is one MLIR dialect family with
  Spinor; the optimizer is a pass pipeline; lowering is
  `mlir-opt`-shaped. Read §1 (the linear type story) and §3
  (the swap policy for borrowed passes).
- **From neither.** Read §1, §2, §3, §4 in order.

The full glossary lives in [`glossary.md`](glossary.md).

## Dependency graph

```
                   M1 dialect
                       │
            ┌──────────┼──────────┐
            │          │          │
        M2 parser   M3 types   M4 lowering
            │                      │
            └─────────┬────────────┘
                      │
              ┌───────┴───────┐
              │               │
        M5 built passes  M6 borrowed passes
              │               │
              └───────┬───────┘
                      │
                M7 orchestration
                  + benchmarks
```

## Critical rules (restated, Phase B emphasis)

- **R1 — Build bottom-up.** Spinor (Phase A) is done; Phonon now
  builds on top. Phase A tests must remain green at every commit.
- **R2 — The optimizer lives here.** Spinor only specialises;
  Phonon optimises. If you want to write a circuit-shrinking
  pass in `spinor/`, **stop**. That work belongs in
  `phonon/optimizer/`.
- **R3 — One C++ engine.** The dialect, type checker, lowering,
  and optimizer are C++ on MLIR/LLVM. Python appears only at the
  edges: the borrowed PyZX adapter (subprocess + JSON cassette)
  and the pytket benchmark harness. There is no second compiler
  in Python.
- **R4 — Re-verify and pin.** Versions re-verified
  2026-06-16 (see [`../versions.md`](../versions.md) Phonon
  block). PyZX is now under the `zxcalc` GitHub org
  (Apache-2.0); tweedledum is C++17 / MIT (last tag 1.1.1, 2021)
  and stays behind an owned interface for replaceability.
- **R5 — No vendor optimizer in our path.** Qiskit is never
  invoked for optimisation; its only Phase B appearance is in
  the M7 benchmark harness, run as a comparison baseline.

## Milestone scope summary

| M | What lands | Tests |
| --- | --- | --- |
| 1 | Phonon dialect: control-flow ops + classical types. | builder, round-trip, structural verify, golden print. |
| 2 | Recursive-descent parser, containment proof. | spinor-corpus parses unchanged, structured corpus parses. |
| 3 | Linear type checker. | Legality corpus: cloning, use-after-measure, discard. |
| 4 | Lowering Phonon → flat Spinor. | Worked example byte-stable; lowered IR runs Phase A unchanged. |
| 5 | Built optimizer passes. | Reduction benchmarks; equivalence preserved every step. |
| 6 | Borrowed-pass adapters (tweedledum, PyZX/quizx). | Interface contract; null-impl path; cassette tests. |
| 7 | Pipeline + benchmarks. | E2E corpus through Phonon → Spinor → chip; pytket / Qiskit comparison. |

## Where to put new code

| You're adding… | Folder |
| --- | --- |
| A control-flow op | `phonon/dialect/` |
| A grammar rule | `phonon/parser/cpp/` |
| A linear-type rule | `phonon/types/` |
| A lowering pattern | `phonon/lower/` |
| A built optimizer pass | `phonon/optimizer/` |
| A borrowed-pass adapter | `phonon/optimizer/borrowed/` |
| A test | `phonon/tests/<m1..m7>/` |
| Anything in Spinor or Photon | **Stop.** Wrong phase. |
