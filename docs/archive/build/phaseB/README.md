# Phase B (Phonon) — engineering specs

Per-milestone engineering documents for the Phonon phase. Each
`Mxx_*.md` follows the [`spec_template.md`](spec_template.md)
shape (10 sections). Read [`M0_overview.md`](M0_overview.md) first
for the dependency graph and the three-audience on-ramps.

For the journal of what landed when, see
[`../phaseB_progress.md`](../phaseB_progress.md). For deviations
from the Phonon Engineering Deep-Dive, see
[`../phaseB_decisions.md`](../phaseB_decisions.md). For the
end-of-phase user guide, see
[`../phaseB_phonon_guide.md`](../phaseB_phonon_guide.md).

| File | Milestone |
| --- | --- |
| [`M0_overview.md`](M0_overview.md) | Dependency graph, on-ramps, glossary stub. |
| [`M1_dialect.md`](M1_dialect.md) | Phonon MLIR dialect (extends Spinor). |
| [`M2_parser.md`](M2_parser.md) | Recursive-descent parser, superset grammar, containment. |
| [`M3_linear_types.md`](M3_linear_types.md) | Linear type system (no-cloning). |
| [`M4_lowering.md`](M4_lowering.md) | Phonon → Spinor lowering passes. |
| [`M5_optimizer_built.md`](M5_optimizer_built.md) | Cancellation, merging, reorder, schedule. |
| [`M6_optimizer_borrowed.md`](M6_optimizer_borrowed.md) | Tweedledum, PyZX/quizx behind owned interfaces. |
| [`M7_orchestration.md`](M7_orchestration.md) | Pipeline + benchmarks (pytket, Qiskit). |
| [`glossary.md`](glossary.md) | Every Phonon term, defined once. |
| [`test-matrix.md`](test-matrix.md) | Single-page index of all Phase B tests. |
| [`dialect-extension.md`](dialect-extension.md) | How Phonon's dialect extends Spinor's. |
