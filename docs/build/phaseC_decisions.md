# Phase C (Photon) — decisions log

One entry per design decision made during Phase C. Each entry:

- the decision,
- alternatives considered,
- why this won,
- the spec section (Photon & Frontends Deep-Dive part / section)
  it relates to.

Append-only. For per-milestone engineering specs see
[`phaseC/`](phaseC/README.md).

---

## 2026-06-16 — D13: Photon ships its own `.pho` source surface (not library-only)

**Decision.** Photon is a first-class third front door alongside
the Python decorator and the C++ ingester. `.pho` is its own file
extension; a recursive-descent C++ parser in `photon/lang/parser/`
produces Phonon IR.

**Alternatives.**

- **Library-only Photon.** Drop the `.pho` source surface; expose
  `QReg` and `photon.lib` as a C++/Python API only. Smaller
  surface, but it collapses the convergence story (M6) from
  three doors to two and contradicts the deep-dive's
  "kernel search(oracle: Oracle) -> int { ... }" worked example,
  which is plainly source code.
- **Embed Photon syntax inside Phonon's parser.** Risks blurring
  the layer boundary (Phonon is meant to be the chip-independent
  middle, not the OO surface), so rejected.

**Why this won.** Three genuinely independent translators that
all converge on Phonon is the architectural proof in M6. A
recursive-descent C++ parser keeps Photon thin (no engine
re-implementation) and reuses the Phase A/B parser pattern that
is already proven in tree.

**Section reference.** Photon Deep-Dive Part 1 §3 (worked
example) and Part 2 §7 (three doors, one engine).

## 2026-06-16 — D14: M6 e2e tests use simulator + recorded HTTP cassettes

**Decision.** The convergence test at M6 runs through the full
Phase A pipeline and submits via Phase A's M10 cassette path:
hermetic CI with no provider tokens. A one-time live submission
to a simulator-backed provider endpoint records the cassette;
PRs replay it.

**Alternatives.**

- **Sim only, never live.** Hermetic but never proves the
  submission boundary works.
- **Live submissions in CI.** Requires every contributor to have
  IBM/Braket/Azure tokens; rejected.

**Why this won.** Mirrors Phase A's M10 strategy exactly. The
"runs end-to-end to a histogram" claim in Plan C M1 is real, not
stubbed; CI stays hermetic.

**Section reference.** Photon Deep-Dive Part 2 §7; Plan C
"Definition of done" bullet 1.

## 2026-06-16 — D15: LLVM/Clang/MLIR pin bumped to 22.1.8 for Phase C

**Decision.** Phase C pins **LLVM/Clang/MLIR 22.1.8** (released
2026-06-16, the final 22.1.x). Phase A and Phase B were on
22.1.7; the bump is verified clean by re-running their test
suites first.

**Alternatives.**

- Stay on 22.1.7 — but the deep-dive says "current LLVM/Clang"
  and 22.1.8 is the final 22.x release; staying behind invites a
  later rebase.
- Wait for 23.x — branches 2026-07-14, GA 2026-08-25; would block
  Phase C for >2 months.

**Why this won.** 22.1.8 is the last 22.1 patch (per LLVM Weekly
#649 / discourse); pinning it now avoids re-pinning mid-Phase C.

**Section reference.** RULE 4; Plan C "re-verify and pin
versions" preface.

## 2026-06-16 — D16: nanobind pinned at 2.12.0 (well above the deep-dive's 2.4 floor)

**Decision.** Phase C pins **nanobind 2.12.0** (BSD-3-Clause,
released 2025-02-25), exposed via the `python/nanobind` PyPI
distribution and built from source via CMake `FetchContent`.

**Alternatives.**

- pybind11. Larger binaries, slower compiles, no Stable-ABI
  story.
- Hand-rolled CPython C API. Excessive for a thin binding.

**Why this won.** The deep-dive endorses nanobind explicitly.
2.12.0 is the latest stable; the floor was ≥ 2.4. The library is
single-header, MIT-style permissive, has CMake hooks, and is
adopted by adjacent quantum projects (PennyLane Catalyst).

**Section reference.** Photon Deep-Dive Part 2 §5.3.

## 2026-06-16 — D17: CPython floor 3.12, target 3.13

**Decision.** The decorator targets CPython **3.13**; **3.12 is
the supported floor** (matches the deep-dive's "3.12+" wording).
Older Pythons are rejected by the decorator at import time.

**Alternatives.**

- 3.11 floor. Misses `ast.parse(..., optimize=...)` and the
  stricter AST constructors that catch frontend bugs early.
- 3.13 floor (no 3.12 support). Cuts off many users still on
  3.12 distributions.

**Why this won.** 3.12 is the deep-dive's stated floor; 3.13 is
the latest stable line and adds optimizer-aware AST parsing that
the decorator uses for its safety checks.

**Section reference.** Photon Deep-Dive Part 2 §5.2; RULE 4.
