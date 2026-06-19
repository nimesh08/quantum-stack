# Cross-phase decisions

Decisions that span more than one phase (version pins, monorepo
boundaries, CI policy, etc.). One entry per decision. Append-only.

---

## 2026-06-16 — Session 0: repo skeleton bootstrapped

**Decision.** Create the `quantum-stack/` monorepo exactly as described in
`docs/specs/00_implementation_handoff_and_repo_strategy.docx` §3, with one
top-level CMake project covering the C++ engine (Spinor + Phonon + Photon
core) and a non-CMake `platform/` folder for FastAPI + React.

**Alternatives considered.**
- Multi-repo split per phase. Rejected: lowering forces upper layers to
  track lower-layer dialect changes constantly; cross-repo versioning is
  pure friction.
- Pre-splitting `platform/` into its own repo. Rejected: the brief says
  "do not pre-split"; extraction is allowed *later* if monorepo friction
  becomes painful.

**Why it won.** Matches the brief verbatim and keeps atomic dialect-plus-
consumer changes possible in one commit.

**Spec link.** §3 ("Repository strategy: one monorepo").

---

## 2026-06-16 — LLVM/MLIR pinned to 22.1.7

**Decision.** Pin `QSTACK_LLVM_VERSION` to `22.1.7` in
`cmake/Versions.cmake`.

**Alternatives considered.**
- 22.1.8 (scheduled 2026-06-16, the day of Session 0). Rejected for now:
  it is the *scheduled* but not necessarily *published* release at the
  moment of pinning, and 22.1.8 is announced as the final 22.1.x release
  before the 23.x branch on 2026-07-14. Bump to 22.1.8 the moment its
  GitHub tag is live.
- 22.1.6 / earlier. Rejected: 22.1.7 (2026-06-02) is current stable and
  is the latest published patch in the 22.1.x line on the pin date.
- 23.x. Rejected: not branched yet (release/23.x branches 2026-07-14).

**Why it won.** Latest published 22.1.x at pin time, matches the brief's
"LLVM/MLIR 22.1.x line" RULE 4 correction.

**Spec link.** RULE 4 in the implementation handoff.

---

## 2026-06-16 — Eigen pinned to 5.0.1 (GitLab `libeigen/eigen`)

**Decision.** Pin `QSTACK_EIGEN_VERSION` to `5.0.1`.

**Alternatives considered.**
- Eigen 3.4.x. Rejected explicitly by RULE 4.
- Tuxfamily mirror. Rejected: the canonical home is GitLab
  (`libeigen/eigen`); RULE 4 calls this out.
- Eigen 5.0.0 (2025-09-30). Rejected: 5.0.1 (2025-11-11) is a strict
  superset (alignment fixes, `<version>` header, BLAS/LAPACK on Windows).

**Why it won.** Latest 5.0.x patch on GitLab.

**Spec link.** RULE 4.

---

## 2026-06-16 — Platform services excluded from top-level CMake

**Decision.** `platform/jobsvc/` (FastAPI) and `platform/playground/`
(React) are not `add_subdirectory()`'d from the root CMake. They have
their own tooling (uv/pip and pnpm respectively, to be set in Phase D).

**Why it won.** They are different tech stacks; pulling them under CMake
would require artificial wrappers. Brief explicitly contemplates a future
extraction of these services.

**Spec link.** §3 "The one allowed future split".
