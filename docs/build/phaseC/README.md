# Phase C — Photon and the Frontends

This folder hosts the per-milestone engineering specs for Phase C
(`photon/`). Each `Mxx_*.md` follows [`spec_template.md`](spec_template.md).

| File | What it covers |
| --- | --- |
| [`M0_overview.md`](M0_overview.md) | Phase C TOC, on-ramps, dependency graph, rules |
| [`spec_template.md`](spec_template.md) | The ten-section spec template |
| [`M1_lang.md`](M1_lang.md) | Photon language core (`.pho` parser + OO-to-Phonon) |
| [`M2_lib.md`](M2_lib.md) | `photon.lib` standard algorithms |
| [`M3_nanobind.md`](M3_nanobind.md) | C++ ↔ Python binding |
| [`M4_python_frontend.md`](M4_python_frontend.md) | `@photon.kernel` decorator |
| [`M5_cpp_frontend.md`](M5_cpp_frontend.md) | Clang LibTooling ingester |
| [`M6_convergence.md`](M6_convergence.md) | Convergence check (three doors, one IR) |
| [`glossary.md`](glossary.md) | Phase C terms (extends Phase A/B glossaries) |
| [`test-matrix.md`](test-matrix.md) | Single-page index of every Phase C test |

For the build journal see
[`../phaseC_progress.md`](../phaseC_progress.md). For deviations from
the Photon Deep-Dive see [`../phaseC_decisions.md`](../phaseC_decisions.md).
The end-of-phase user guide is
[`../phaseC_photon_guide.md`](../phaseC_photon_guide.md).
