# docs/specs/

The six specification documents (design blueprints) and this implementation
handoff. These are inputs to the build; do not edit them as part of phase
work — file an issue or note a deviation in `docs/build/decisions.md`
instead.

| #   | Document                            | Read for phase  |
|-----|-------------------------------------|-----------------|
| 0   | Implementation Handoff & Repo Strategy (this brief — `00_implementation_handoff_and_repo_strategy.docx`) | All phases |
| 1   | Vision & Technical Concept          | All phases (read first) |
| 2   | Build-Stack Components & References | All phases      |
| 3   | Spinor Engineering Deep-Dive        | Phase A         |
| 4   | Phonon Engineering Deep-Dive        | Phase B         |
| 5   | Photon & Frontends Deep-Dive        | Phase C         |
| 6   | Platform Services Deep-Dive         | Phase D         |

## What to attach to each phase chat

Concept (#1) + Scaffold (#2) + that phase's deep-dive (#3 / #4 / #5 / #6) +
that phase's implementation plan. The agent does **not** need other phases'
deep-dives to do the current phase.

## Pinned versions

The implementation handoff calls out version pin corrections (RULE 4) that
have already been applied to `cmake/Versions.cmake`:

- Eigen 5.0.x lives on **GitLab** (`libeigen/eigen`), not tuxfamily; not 3.4.
- LLVM/MLIR is on the **22.1.x** line.
- `rustworkx` is **prototype-only** (Python/Rust); the C++ engine
  implements its own graph routines.

Re-verify before touching `cmake/Versions.cmake`.
