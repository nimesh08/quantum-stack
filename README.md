# quantum-stack

The **Photon · Phonon · Spinor** quantum compiler stack — one monorepo,
four phased plans, one binary.

> Status: empty, compiling skeleton. Session 0 (this commit) sets up the
> repo layout, top-level CMake, CI scaffolding, and docs. No compiler
> logic has been written yet. Phase A (Spinor) starts next, in its own
> chat per the handoff brief.

The implementation handoff lives at
[`docs/specs/00_implementation_handoff_and_repo_strategy.docx`](docs/specs/00_implementation_handoff_and_repo_strategy.docx).
Read it before doing any phase work.

---

## Repository layout

```
quantum-stack/
├── CMakeLists.txt          # top-level C++ build (LLVM/MLIR)
├── cmake/Versions.cmake    # pinned third-party versions (single source of truth)
├── spinor/                 # Phase A — per-chip layer
│   ├── dialect/            # Spinor MLIR dialect (TableGen)
│   ├── parser/             # Lark prototype, then C++ recursive descent
│   ├── passes/             # placement, routing (SABRE), decomposition, cleanup
│   ├── registry/           # per-chip YAML + loader + schema
│   ├── sim/                # Stim wrapper + statevector engine
│   ├── emit/               # OpenQASM 3 / QIR / Quil emitters
│   ├── submit/             # provider adapters (behind one interface)
│   └── tests/
├── phonon/                 # Phase B — chip-independent layer
│   ├── dialect/            # Phonon dialect (extends spinor/dialect)
│   ├── lower/              # unroll / inline / flatten -> Spinor
│   ├── types/              # linear type checker (no-cloning)
│   ├── optimizer/          # cancellation, merging, reorder, ZX, synth, schedule
│   └── tests/
├── photon/                 # Phase C — language + frontends
│   ├── lang/               # OO language + photon.lib
│   ├── frontends/python/   # @photon.kernel (ast/inspect)
│   ├── frontends/cpp/      # Clang LibTooling ingester
│   ├── bindings/           # nanobind C++<->Python
│   └── tests/
├── platform/               # Phase D — services (NOT in top-level CMake)
│   ├── jobsvc/             # FastAPI service + workers + queue
│   ├── calibration/        # nightly refresh job
│   ├── playground/         # React 19 + Monaco SPA
│   └── deploy/             # Dockerfiles, compose
├── docs/
│   ├── specs/              # the six specification documents (+ this brief)
│   └── build/              # progress logs, usage guides, decisions per phase
├── .github/workflows/      # CI scaffolding (build + lint)
└── scripts/                # helper scripts (empty for now)
```

The four layers are not independent products — they are one compiler
engine that compiles to a single binary, and they share the MLIR dialects
(Phonon's dialect extends Spinor's).

---

## Build order (bottom-up, one chat per phase)

```
Chat 1  →  Plan A  →  builds spinor/    (finish + tests pass before moving on)
Chat 2  →  Plan B  →  builds phonon/
Chat 3  →  Plan C  →  builds photon/
Chat 4  →  Plan D  →  builds platform/
```

Each chat is handed: **concept (#1) + scaffold (#2) + that phase's
deep-dive + that phase's implementation plan**. The agent does not need
the other phases' deep-dives to do the current phase.

A phase is finished when its done-criteria are met and its tests pass —
only then start the next conversation.

---

## The seven critical rules

These travel with every phase. They are stated once here as the master
copy.

> **RULE 1 — Build bottom-up.** Do not start this phase until the phase
> below it is complete and its tests pass.
> **Spinor → Phonon → Photon → Platform.** A finished lower layer is a
> real, testable artifact the next layer depends on.

> **RULE 2 — Optimization lives in Phonon, never in Spinor.** Spinor only
> specializes a program to one chip (placement, routing, decomposition,
> tiny post-decomposition cleanup). The heavy optimizer runs once, above
> all chips, in Phonon. If you are writing a circuit-shrinking optimizer
> inside Spinor, stop — that is an architecture error.

> **RULE 3 — One C++ engine, one source of truth.** The production engine
> is C++ on MLIR/LLVM. Python appears in exactly two places: a throwaway
> Lark prototype parser (replaced by hand-written C++), and the thin
> nanobind binding. Never build a second compiler in Python.

> **RULE 4 — Re-verify and pin every version before coding.** Versions in
> these docs were verified June 2026 and drift. Check current releases,
> then pin exact versions in the build files. Known corrections already
> applied: **Eigen 5.0.x (GitLab, not tuxfamily, not 3.4)**;
> **rustworkx is Python/Rust prototype-only** (the C++ engine implements
> its own graph routines); **LLVM/MLIR 22.1.x line**.

> **RULE 5 — Submit to providers in verbatim / pass-through mode only.**
> Never let a vendor compiler re-optimize our output. This is
> load-bearing: it is the only way the result quality is provably ours.

> **RULE 6 — Phase E is out of scope.** Automatic synthesis of quantum
> programs from unmodified classical code (the research summit) is
> deliberately not in this build. Do not build toward it. It becomes
> reachable only once Phases A–D exist.

> **RULE 7 — The names are working names.** *Photon*, *Phonon*, and
> *Spinor* are locked for development only. Run a trademark search before
> any public use.

---

## Pinned versions (Session 0, 2026-06-16)

The single source of truth is [`cmake/Versions.cmake`](cmake/Versions.cmake).
Human-readable rationale is in
[`docs/build/versions.md`](docs/build/versions.md).

| Component   | Pinned   | Source                              | Notes |
|-------------|----------|-------------------------------------|-------|
| LLVM / MLIR | **22.1.7** | github.com/llvm/llvm-project      | 22.1.7 is the latest published 22.1.x patch (2026-06-02). 22.1.8 is the scheduled final 22.1.x (2026-06-16); bump once tagged. Then 23.1.x (release/23.x branches 2026-07-14). |
| CMake (min) | **3.28**   | cmake.org                          | Latest stable at pin date is 4.3.3; we only require 3.28+. |
| C++ standard| **17**     | —                                  | Sufficient for Eigen 5.x and the engine's needs. |
| Eigen       | **5.0.1**  | gitlab.com/libeigen/eigen          | RULE 4 correction: GitLab, not tuxfamily; not 3.4. 5.0.1 (2025-11-11) is current. |

Stim, nanobind, and Lark are pinned in their respective phases (A and C)
where they are first introduced.

---

## Building the skeleton

```bash
# Configure (skeleton compiles even without LLVM; warnings only).
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build interface targets.
cmake --build build -j
```

To configure with LLVM/MLIR present (required from Phase A onward):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/path/to/llvm-22.1.7/lib/cmake/llvm \
  -DMLIR_DIR=/path/to/llvm-22.1.7/lib/cmake/mlir
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Platform services in `platform/` are **not** built by CMake; they have
their own tooling and are introduced in Phase D.

---

## How to run a phase chat

A starter prompt template (the agent fills in `<PHASE>` and attaches the
listed files):

> You are implementing `<PHASE>` of the Photon · Phonon · Spinor quantum
> compiler stack.
>
> Attached: the Vision & Technical Concept, the Build-Stack scaffold, the
> `<PHASE>` deep-dive, and the `<PHASE>` implementation plan.
>
> Work inside this single monorepo, in the folder this plan specifies.
> Follow the plan's build order and milestones. Honor the critical rules
> in the plan (bottom-up; optimization lives in Phonon not Spinor; one
> C++ engine; re-verify and pin versions; verbatim submission only).
>
> Before writing code: re-verify the current versions of every dependency
> the plan lists, and pin them. Then proceed milestone by milestone, with
> tests at each milestone. Stop at the definition of done and report
> status.

---

## Where the agent writes its trail

For every phase, three files in [`docs/build/`](docs/build/):

- `phase-<x>-progress.md` — append-only journal, one dated entry per session.
- `phase-<x>-usage.md` — user-facing usage guide for the phase's artifacts.
- `phase-<x>-decisions.md` — design decisions, alternatives, and links back
  to the spec.

Plus two shared files: `decisions.md` (cross-phase) and `versions.md`
(companion to `cmake/Versions.cmake`).

See [`docs/build/README.md`](docs/build/README.md) for the conventions.

---

## License

TBD before any public release. (See RULE 7: trademark search required
before public use of the working names.)
