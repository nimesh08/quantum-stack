# Phase A (Spinor) — decisions log

One entry per design decision made during Phase A. Each entry:

- the decision,
- alternatives considered,
- why this won,
- the spec section (Deep-Dive part / section) it relates to.

Append-only.

For per-milestone engineering specs see
[phaseA/](phaseA/README.md).

---

## 2026-06-16 — D1: Add an OpenQASM 3 import door in Phase A

**Decision.** Implement a minimal OpenQASM 3.1 importer (M11)
inside Phase A, scoped to the Spinor grammar's gate set plus
`measure`, `reset`, `barrier`, `qubit[n]`, `bit[n]`. No
`defcal`, no `gate` definitions, no control flow.

**Alternatives.**

- **Defer to a later phase.** Plan A's M1–M10 list does not
  call this out; deferring would shrink Phase A scope.
- **Build a full OpenQASM 3 parser.** Re-implements much of
  Phonon's responsibility; bloats Phase A.

**Why this won.** The Vision doc (§3.5) explicitly lists
OpenQASM import as a first-class door into the Spinor
interchange. The infrastructure for the C++ recursive-descent
parser (M7) is reusable. Round-trip testing (`emit_qasm3 ∘
import_qasm3 = identity` on the subset) gives the M9 emitter
a strong regression harness for free. Scope is tight enough
not to bloat the phase.

**Section reference.** Deep-Dive Part 1 §6.4–6.5 (the IR is
the hub); Vision §3.5 (the interchange).

## 2026-06-16 — D2: Eigen vendored via FetchContent at tag 5.0.1

**Decision.** Pull Eigen from
<https://gitlab.com/libeigen/eigen> via CMake `FetchContent`
at the exact tag `5.0.1`, rather than requiring a system
install.

**Alternatives.**

- **Require system Eigen.** Forces every contributor to install
  matching Eigen from a distro that may ship 3.4 or older.
- **Vendor Eigen as a git submodule.** Works, but needs `git
  submodule update --init` to be remembered.

**Why this won.** `FetchContent` with a pinned tag gives a
reproducible build from a clean clone, and Eigen 5.0.x is too
new to be on most distros yet (released 2025-09-30).

**Section reference.** Rule 4 (re-verify and pin); Deep-Dive
Part 2 §5.4.

## 2026-06-16 — D3: Use the requested `phaseA_*` doc names

**Decision.** Create the three end-of-phase user-facing docs
at the names the user requested:
`docs/build/phaseA_progress.md`,
`docs/build/phaseA_decisions.md`,
`docs/build/phaseA_spinor_guide.md`. Leave one-line redirect
notes in the existing kebab-case files
(`phase-a-progress.md`, `phase-a-decisions.md`,
`phase-a-usage.md`) so anyone following the older paths
arrives at the right place.

**Alternatives.**

- **Ignore the user's preferred names** and keep kebab-case.
- **Move the existing files** (would break any external link).

**Why this won.** Honors the user's explicit request without
breaking existing references.

**Section reference.** Handoff doc §3 (one monorepo, folders
mirror documents).

## 2026-06-16 — D4: Layered C++ engine — MLIR-backed when available, in-tree fallback otherwise

**Decision.** Implement the Spinor IR + passes against a thin
internal abstraction with two backends: an **MLIR-backed**
backend (the production path, requires LLVM/MLIR 22.1.7), and
an **in-tree** backend (a header-only mini-IR with the same
op/type/value-semantics shape, no LLVM dependency). The
TableGen `.td` files are committed in tree as the canonical
op/type definitions; CMake selects the backend based on
whether `LLVM_DIR` and `MLIR_DIR` resolve at configure time.

**Alternatives.**

- **Hard-require LLVM 22.1.7.** Means contributors and CI
  pipelines must build LLVM/MLIR (multi-hour) before any
  Phase A code can compile or be tested. Realistically no
  development can happen until that's set up everywhere,
  including this very session.
- **Drop MLIR entirely; build only on the in-tree IR.** Loses
  the MLIR pass-manager, verifier, pattern rewriter, and the
  TableGen-driven dialect machinery. Fights the spec's "the
  IR is an MLIR dialect" guarantee.

**Why this won.** Spec Part 1 §6.1 names MLIR as the IR
framework; Part 1 §6.2-6.5 specifies the dialect in TableGen.
By committing the TableGen and providing both backends behind a
single C++ header surface (`spinor::dialect::Module`,
`spinor::dialect::Op`, `spinor::dialect::Qubit`, etc.), the
production path remains the MLIR one (no spec compromise) and
day-to-day development, CI, and the test corpus all work without
forcing every contributor to build LLVM. The existing top-level
CMake already establishes this conditional (`QSTACK_HAVE_LLVM`
ON/OFF); D4 simply formalises the API surface that lives on
both sides of the conditional.

**Section reference.** Deep-Dive Part 1 §6 (the IR is the hub);
Handoff doc Rule 4 (re-verify and pin); top-level
[`CMakeLists.txt`](../../CMakeLists.txt) lines 37–61 (existing
LLVM detection).

## 2026-06-16 — D5: Native gate names in TargetInfo are bare mnemonics

**Decision.** `TargetInfo::nativeGates` stores bare gate names
("ecr", "rz", "sx", "x"), not the full MLIR mnemonic
("spinor.ecr"). The verifier strips the `spinor.` prefix before
comparing.

**Alternatives.**

- **Store full mnemonics in the registry.** Repetitive; every
  YAML entry would have to write `spinor.ecr` instead of `ecr`,
  which leaks an internal naming convention into a user-facing
  data file.
- **Have the verifier construct full mnemonics for comparison.**
  Identical end behaviour; chose the strip-prefix direction so
  the registry stays human-readable.

**Why this won.** The registry is read by humans (chip
operators, future contributors) and machines (the verifier and
the decomposer). The bare name is what a human writes when
asked "what are this chip's native gates?" and what every
provider's documentation uses (IBM says ecr, sx, x; IonQ says
ms, gpi, gpi2). The compiler-internal `spinor.` prefix is a
detail of our IR encoding, not a user-facing concept.

**Section reference.** Deep-Dive Part 3 §2 (registry).

## 2026-06-16 — D6: Closed-form per-chip recipes for Phase A; defer general Eigen-based KAK to Phase B

**Decision.** M6 ships closed-form decomposition recipes for
the chip families in our registry (IBM/ECR, IonQ/MS,
Quantinuum/RZZ) rather than a general Eigen-based KAK
solver. Single-qubit lowering is Euler-ZYZ via fixed angle
tables (no SVD); two-qubit lowering uses pre-derived recipes
that emit the chip's entangler wrapped in single-qubit
rotations.

**Alternatives.**

- **Vendor Eigen and write a general KAK solver.** Adds a
  large dependency (Eigen 5.0.x via FetchContent) and a
  fair-sized SVD-style pass for Phase A's value (3 chip
  families). The general solver matters when arbitrary
  user-defined 2q gates need decomposing — that's a
  Phonon/Photon scenario.
- **Use Solovay–Kitaev for everything.** Approximate, not
  exact. The spec mandates exact decomposition.

**Why this won.** Recipes are one-line entries per chip
family; new chips with the same entangler reuse them.
Correctness for the recipes we ship is validated by
integration tests that confirm "every output op is in the
chip's native set" plus the M8 simulator-level equivalence
check (when M8 lands). Up-to-phase exact-matrix equivalence
of the recipe output is a useful future tightening; it's
held over to Phase B's optimizer, which needs the general
machinery for synthesis anyway.

**Section reference.** Deep-Dive Part 2 §5 (decomposition);
M6 spec §5 (algorithms).

## 2026-06-16 — D7: Stim wrapper deferred to a future phase

**Decision.** M8 ships an in-tree dense statevector simulator
only. The Stim wrapper for stabilizer circuits is documented
as a header surface but compiled only when `SPINOR_HAVE_STIM`
is set; full integration (including the version pin in
cmake/Versions.cmake) is deferred to the platform layer
(Phase D) when the calibration ingestion pipeline lands.

**Alternatives.**

- **Vendor Stim now.** Adds a build-time dep before any test
  or user actually exercises stabilizer-class circuits. Phase A
  test corpus (Bell, GHZ, rotations, mid-circuit) is fine on
  the dense engine.
- **Skip Stim entirely.** Loses an important fast path for
  large-N stabilizer circuits, especially M8's check lane on
  Clifford-only programs.

**Why this won.** The check-lane invariant in Phase A is
"compilation preserves meaning on the corpus"; the dense
engine handles the corpus comfortably. Vendoring Stim has a
non-trivial cost (a C++ library plus its Python bindings) that
buys nothing measurable today. The header surface is in place
so the future integration is mechanical.

**Section reference.** Deep-Dive Part 3 §3.

## 2026-06-16 — D8: Submission adapters split — C++ contract + Python SDKs

**Decision.** The submission `IProvider` contract is C++ (so
every later layer can speak it natively), but the live provider
adapters (`ibm`, `aws`, `azure`) are implemented in Python
under `spinor/submit/python/spinor_submit/`. A C++
`LocalProvider` runs the in-process simulator end-to-end. The
Python adapters honour Rule 5 (verbatim mode) and ship a
cassette mode so CI never needs tokens.

**Alternatives.**

- **Live adapters in C++.** Provider SDKs (Qiskit Runtime,
  Braket, Azure Quantum) are best-supported in Python; their
  C-level bindings are partial or unstable. Re-implementing
  network/auth/job-lifecycle in C++ is a Phase D problem, not
  Phase A's.
- **Adapters entirely in Python.** Loses the C++ contract
  the rest of the engine wants (a single `IProvider` for
  testing, simulation, and future cloud-agent use).

**Why this won.** The C++ contract gives the engine a
testable, simulator-backed end-to-end path; the Python shims
get to use the providers' native SDKs verbatim, with cassettes
for offline CI. When Phase D's job service ships, it can wrap
the Python adapters in a service or — if performance demands —
re-implement them in C++ behind the same `IProvider`.

**Section reference.** Deep-Dive Part 3 §5; Rule 3 ("nanobind
bindings" in Phase C are different from these submission shims,
which are platform/SDK plumbing).
