# Pinned versions

Human-readable companion to `cmake/Versions.cmake`. The CMake file is the
source of truth; this file explains *why* and *when next to bump*.

Last verified upstream: **2026-06-16 (UTC)**.

| Component   | Pinned   | Source                              | Verified   | Next bump trigger |
|-------------|----------|-------------------------------------|------------|-------------------|
| LLVM / MLIR | 22.1.8   | github.com/llvm/llvm-project        | 2026-06-16 | 23.1.0 once stable (23.x branches 2026-07-14, GA 2026-08-25). 22.1.8 is the final 22.1.x release. |
| CMake (min) | 3.28     | cmake.org                           | 2026-06-16 | Only if a feature in 4.x is needed; latest stable at pin date is 4.3.3. |
| C++ std     | 20       | —                                   | 2026-06-16 | Bumped 17→20 in M1 (defaulted operator==, std::span, std::variant). |
| Eigen       | 5.0.1    | gitlab.com/libeigen/eigen           | 2026-06-16 | Next 5.0.x patch or 5.1.0. NOT tuxfamily, NOT 3.4. |
| Stim        | (Phase A)| github.com/quantumlib/stim          | —          | Verify and pin during Phase A. |
| nanobind    | 2.12.0   | github.com/wjakob/nanobind          | 2026-06-16 | Phase C pin (BSD-3-Clause, 2025-02-25). Deep-dive floor was >= 2.4. Next bump trigger: 2.13.0 once available. |
| CPython     | 3.13 / 3.12 floor | python.org                 | 2026-06-16 | Phase C target / floor. Deep-dive asked for 3.12+; 3.13 adds `ast.parse(optimize=...)` and stricter constructors that the decorator uses. |
| Lark        | 1.3.1    | github.com/lark-parser/lark         | 2026-06-16 | Throwaway prototype only; pinned in spinor/parser/lark/pyproject.toml; remove with the prototype at M7. |

## Procedure to bump a pin

1. Re-verify upstream (release notes, schedule, breaking changes).
2. Edit `cmake/Versions.cmake` (single source of truth).
3. Add an entry to `docs/build/decisions.md` with the alternatives and the
   rationale.
4. Update the row above and the "Last verified" date.
5. Run CI; only land the bump if green.

## RULE 4 corrections already applied

Per the handoff brief:
- Eigen → 5.0.x on **GitLab**, not tuxfamily; not 3.4.
- `rustworkx` is Python/Rust prototype-only; the C++ engine implements
  its own graph routines (so no rustworkx pin in CMake).
- LLVM/MLIR → 22.1.x line.
