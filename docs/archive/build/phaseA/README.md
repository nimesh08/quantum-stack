# Phase A — Spinor — Engineering Specs

This folder is the **mistake-proofing layer** for Phase A. The
top-level engineering plan was the strategic index; this folder is
where every milestone is nailed down in detail before any code is
written.

## How to read this folder

Read in order:

1. **[M0_overview.md](M0_overview.md)** — start here. Goals,
   dependency graph, three-audience on-ramps, glossary stub.
2. **[spec_template.md](spec_template.md)** — the 10-section
   template every Mxx.md uses. If you're contributing a new
   milestone spec, copy this.
3. **M1–M11 specs** in build order. Each contains a literal test
   matrix table and a worked cookbook example.
4. **[registry-schema.md](registry-schema.md)** — the canonical
   YAML schema for chip entries (read while doing M4 onward).
5. **[test-matrix.md](test-matrix.md)** — single-page index of
   every test in Phase A, kept current as milestones land.
6. **[glossary.md](glossary.md)** — every quantum / compiler /
   MLIR term used anywhere in Phase A.

## Milestone index

| #   | File                                        | Role                                         |
| --- | ------------------------------------------- | -------------------------------------------- |
| M0  | [M0_overview.md](M0_overview.md)            | Phase entry, dependency graph, on-ramps      |
| M1  | [M1_dialect.md](M1_dialect.md)              | Spinor MLIR dialect (types, ops, verifier)   |
| M2  | [M2_lark_parser.md](M2_lark_parser.md)      | Lark prototype parser (Python, deleted at M7) |
| M3  | [M3_verifier.md](M3_verifier.md)            | W1–W6 + type-check verifier                  |
| M4  | [M4_registry.md](M4_registry.md)            | Per-chip YAML registry + loader              |
| M5  | [M5_placement_routing.md](M5_placement_routing.md) | Placement + SABRE routing in C++      |
| M6  | [M6_decomposition.md](M6_decomposition.md)  | Euler ZYZ + KAK decomposition + cleanup      |
| M7  | [M7_cpp_parser.md](M7_cpp_parser.md)        | Production C++ recursive-descent parser      |
| M11 | [M11_qasm_import.md](M11_qasm_import.md)    | OpenQASM 3 subset importer                   |
| M8  | [M8_simulator_check_lane.md](M8_simulator_check_lane.md) | Statevector sim + check lane    |
| M9  | [M9_emitters.md](M9_emitters.md)            | OpenQASM 3 / QIR / Quil emitters             |
| M10 | [M10_submission.md](M10_submission.md)      | IBM / Braket / Azure submission adapters     |

## Workflow per milestone

1. Spec lands here first (markdown only, no code).
2. Code is written test-first; tests live under `spinor/tests/Mxx_*`.
3. Each milestone's CI gate must be green before the next starts.
4. Append-only journal at
   [phaseA_progress.md](../phaseA_progress.md).
5. Deviations from the Deep-Dive go in
   [phaseA_decisions.md](../phaseA_decisions.md).
6. End-of-phase user guide at
   [phaseA_spinor_guide.md](../phaseA_spinor_guide.md).
