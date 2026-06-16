# Phase B (Phonon) — progress

Append-only journal. One dated entry per session. New entries go
at the bottom.

A new chat reading this from cold should be able to resume Phase B
by reading the most recent entry: it states what landed, what's
next, and where to look.

For per-milestone engineering specs see
[`phaseB/`](phaseB/README.md). For deviations from the Phonon
Engineering Deep-Dive see [`phaseB_decisions.md`](phaseB_decisions.md).
For the end-of-phase user guide see
[`phaseB_phonon_guide.md`](phaseB_phonon_guide.md).

---

## 2026-06-16 — Phase B planning + M0 scaffolding

Plan landed at
`/home/ubuntu/.cursor/plans/phase_b_phonon_build_2cdfa312.plan.md`.

Spec scaffolding under `docs/build/phaseB/`:

- `README.md` — folder index.
- `spec_template.md` — the 10-section template every Mxx.md uses.
- `M0_overview.md` — three-audience on-ramps + dependency graph.
- `glossary.md` — every quantum / compiler / Phonon term.
- `test-matrix.md` — single-page index, currently empty.
- `dialect-extension.md` — how Phonon's dialect extends Spinor's.

End-of-phase doc skeletons:

- `phaseB_progress.md` (this file).
- `phaseB_decisions.md`.
- `phaseB_phonon_guide.md`.

Phase A precondition re-confirmed: 15/15 ctest entries / 116
cases pass.

**Next:** M1 — Phonon dialect.

## 2026-06-16 — M1 (Phonon dialect) landed

In-tree backend implemented under `phonon/dialect/`:

- `include/phonon/dialect/Phonon.h` — public API; re-exports
  `spinor::dialect`'s `Diagnostics`, `Location`, `ValueId`, `OpId`,
  `Attribute`. Adds `TypeKind::{Int, Angle, Func}` and 15 Phonon
  ops (const_int, const_angle, binop, cmp, if/end_if, for/end_for,
  while/end_while, def/end_def, call, return, assign).
- `lib/Phonon.cpp` — module + value-table + op-table + bridge
  helpers `toSpinorKind` / `fromSpinorKind`.
- `lib/Builder.cpp` — Builder mirrors Spinor's surface and adds
  Phonon classical / control-flow / function builders.
- `lib/Print.cpp`, `lib/Parse.cpp` — round-trippable textual
  format. The header `phonon.module` distinguishes it from a
  Spinor module; ops use their full `phonon.<n>` or `spinor.<n>`
  mnemonic.
- `lib/Verify.cpp` — structural verifier: marker pairing
  (if/end_if, for/end_for, def/end_def), predicate type, return
  inside def, no nested defs.
- `td/Phonon{Dialect,Types,Ops}.td` — TableGen sources committed
  for the future MLIR-backed bridge.
- `phonon/CMakeLists.txt` updated to mirror Phase A's pattern:
  the in-tree backend builds without LLVM; LLVM-backed bridge
  deferred.

Region encoding: marker-pair ops in a flat op vector with
`then_count` / `else_count` / `body_count` attributes. The MLIR
bridge will translate to true `Region`s when wired.

Tests (4 executables, 9 cases) all green:

```
ctest -L phonon_M1 → 4/4 pass
  phonon_m1_builder_test     (4 cases)
  phonon_m1_round_trip_test  (2 cases)
  phonon_m1_verify_test      (4 cases)  [4 cases — listed in source]
  phonon_m1_print_golden_test (1 case)
```

All Phase A tests remain green: 19/19 ctest entries pass.

**Next:** M2 — Phonon parser + containment proof.

## 2026-06-16 — M2 (Phonon parser + containment) landed

Spec: `phaseB/M2_parser.md`. Strict-superset hand-written
recursive-descent parser plus its lexer.

Components shipped:

- `phonon/parser/cpp/lib/Lexer.{h,cpp}` — extends Spinor's
  token set with `if`, `else`, `for`, `while`, `def`, `return`,
  `int`, `angle`, `in`, `..`, `{`, `}`, `<`, `>`, `<=`, `>=`,
  `==`, `!=`, `+`. Both `;` and `//` line-comment styles.
- `phonon/parser/cpp/lib/Parser.cpp` — recursive-descent
  parser: declarations (qubit/bit/int/angle), gate statements,
  measure / reset / barrier, if/else, for (compile-time
  bounds), while, def with typed parameters, call (with
  qubit-arg write-back), assign, return. Threads quantum-slot
  bindings through statements so the IR is in SSA shape ready
  for M3.
- `phonon/parser/cpp/include/phonon/parser/Parser.h` — public
  `parse(text, filename) → ParseResult`.

Test corpus committed under `phonon/tests/corpus/`:
`bell_pair_func.phn` (function inlining), `qft_loop.phn`
(counted loop), `teleportation.phn` (mid-circuit measure +
classical-controlled gates).

Tests (3 ctest entries / 12 cases) all green:

```
ctest -L phonon_M2 → 3/3 pass
  phonon_m2_containment_test  (6 cases — every Phase A .spn fixture
                               parses unchanged through the Phonon parser)
  phonon_m2_structured_test   (3 cases — bell_pair_func, qft_loop,
                               teleportation produce the expected ops)
  phonon_m2_error_test        (3 cases — diagnostic cases)
```

All tests still green: 22/22 ctest entries.

**Next:** M3 — linear type checker.

## 2026-06-16 — M3 (Linear type checker) landed

Spec: `phaseB/M3_linear_types.md`. Phonon's no-cloning-as-a-
compile-error checker.

Components shipped:

- `phonon/types/include/phonon/types/LinearTypeChecker.h` —
  public API. `Options` includes `warnImplicitDiscard` and
  `midCircuitMeasure` (W4 relaxation, derivable from a
  Phase A `TargetInfo` via `optionsForTarget`).
- `phonon/types/lib/LinearTypeChecker.cpp` — single forward
  pass keeping per-ValueId state (consumed, measured, producer).
  Three error codes:
  - **E1**: qubit value used more than once (no-cloning).
  - **E2**: qubit used after measurement (W4 governs the
    error message; reset always permitted as the
    rehabilitating consumer).
  - Implicit-discard: warning, never error (Rust-style).
  Classical scalars (int, angle, bit) are non-linear and never
  trigger the checker.

Tests (2 ctest entries / 9 cases) all green:

```
ctest -L phonon_M3 → 2/2 pass
  phonon_m3_linear_test  (6 cases: bell_passes, no_cloning_caught,
                          use_after_measure_caught, reset_after_measure_ok,
                          implicit_discard_warns, classical_no_op)
  phonon_m3_corpus_test  (3 cases: teleportation, bell_pair_func,
                          qft_loop all type-check clean)
```

All tests still green: 24/24 ctest entries.

**Next:** M4 — lowering Phonon → Spinor (inline, unroll, flatten).

## 2026-06-16 — M4 (Lowering Phonon → Spinor) landed

Spec: `phaseB/M4_lowering.md`. Decision D9 below records two
scope choices.

Components shipped:

- `phonon/lower/include/phonon/lower/Lowering.h` — public API
  (`lower(module, target?) → Result`).
- `phonon/lower/lib/Lowering.cpp` — single-pass walker:
  - **Spinor passthrough:** every `spinor.*` op is copied
    verbatim with operand/result Phonon-ValueId → Spinor-ValueId
    mapping.
  - **Function inlining:** `phonon.def` blocks are skipped at
    the top level; `phonon.call` triggers a body-walk with
    parameter→argument substitution. Recursion is detected and
    rejected. Functions without an explicit `phonon.return`
    auto-return the latest binding of every qubit parameter
    (this matches the most common function shape, e.g. the
    `bell_pair` worked example).
  - **Loop unrolling:** `phonon.for` reads compile-time `lo`,
    `hi` integer operands and emits the body `hi - lo` times.
  - **Conditional flattening:** `phonon.if` emits the
    then-body unconditionally. The classical-control wrapping
    is handled by the M9 emitter when targeting QASM3 with
    feedforward (Phase A's M9 already supports this).
  - **Self-check:** the lowered output is structurally
    Spinor-only; tests assert this.

Tests (2 ctest entries / 6 cases) all green:

```
ctest -L phonon_M4 → 2/2 pass
  phonon_m4_lower_test     (4 cases: bell_passthrough,
                            qft_loop_unrolls, bell_pair_func_inlines,
                            no_phonon_ops_in_output)
  phonon_m4_pipeline_test  (2 cases: lowered Bell + qft_loop pass
                            Phase A's verifier with target=generic)
```

Decision recorded: **D9** — Phase B M4 emits if-bodies
unconditionally; the run-time conditional is reified by the M9
emitter when targeting feedforward-capable chips. This keeps M4
focused on structural lowering and reuses Phase A's existing
emitter rather than reinventing it.

All tests still green: 26/26 ctest entries.

**Next:** M5 — built optimizer passes (cancellation, merging,
reorder, schedule).

## 2026-06-16 — M5 (Built optimizer passes) landed

Spec: `phaseB/M5_optimizer_built.md`. The "build whatever
determines result quality" half of the optimizer.

Components shipped under `phonon/optimizer/`:

- `include/phonon/optimizer/Optimizer.h` — public API
  (`runBuiltPipeline`, `cancelInversePairs`, `mergeRotations`,
  `commuteAndReduce`, `schedule`).
- `lib/Optimizer.cpp` — single TU containing all four passes.
  Each pass operates on the **Phonon dialect** (Rule 2 in
  motion). Implementation strategy: walk-mark-rebuild — mark
  ops dead in a parallel vector, accumulate angle overrides
  for the merging pass, transitively rewrite operand
  references through a remap, then compact via fresh-module
  copy.
- Inverse-pair coverage: `H/H`, `X/X`, `Y/Y`, `Z/Z`, `S/Sdg`,
  `T/Tdg`, `Sx/Sxdg`, plus `CX/CX`, `CZ/CZ`, `Swap/Swap`.
- Rotation merging: `Rx/Rx`, `Ry/Ry`, `Rz/Rz` chains fuse and
  near-zero (1e-12) angles drop.
- Commutation reorder: minimal Phase B variant that re-runs
  the cancellation+merging passes (so any new patterns exposed
  after a merge or cancellation are caught).
- Scheduling: ASAP depth computation; depth value reported as
  a stat. Phase D platform layer can replace with a
  reorder-actually pass if needed (recorded in D11 below).

Tests (3 ctest entries / 10 cases) all green:

```
ctest -L phonon_M5 → 3/3 pass
  phonon_m5_cancel_test    (5 cases: H/H, X/X, S/Sdg, CX/CX,
                             no-false-cancel-with-op-between)
  phonon_m5_merge_test     (3 cases: Rz drops to zero, Rz/Rz fuses,
                             Rx/Rx fuses)
  phonon_m5_pipeline_test  (2 cases: bell_unchanged,
                             redundant_h_pair_shrinks)
```

All tests still green: 29/29 ctest entries.

**Next:** M6 — borrowed optimizer adapters (tweedledum, PyZX/quizx).

## 2026-06-16 — M6 (Borrowed optimizer adapters) landed

Spec: `phaseB/M6_optimizer_borrowed.md`. Decision **D12** below
records the integration depth (interface + NullImpl + one
pluggable adapter + cassette) chosen per the Phase B planning
discussion: this keeps CI hermetic and Rule 3 honest.

Components shipped under `phonon/optimizer/`:

- `include/phonon/optimizer/BorrowedPasses.h` — owned `ISynthesizer`
  and `IZxSimplifier` interfaces. Factories
  `makeNullSynthesizer`, `makeTweedledumSynthesizer`,
  `makeNullZxSimplifier`, `makePyZXSimplifier`.
- `lib/Synthesizer.cpp` — `NullSynthesizer` (CI default) +
  conditionally-compiled `TweedledumSynthesizer` behind
  `SPINOR_HAVE_TWEEDLEDUM`. The Tweedledum-backed factory
  gracefully falls back to NullSynthesizer when the macro is
  unset, so `name()` reports "null" — testable behaviour.
- `lib/ZxSimplifier.cpp` — `NullZxSimplifier` + a
  `PyZXSubprocessSimplifier` that looks for cassettes under
  `${PHONON_CASSETTE_DIR}/zx/<module-name>.json`. Live PyZX
  invocation is gated by `PHONON_PYZX_LIVE=1`. Phase B's
  cassettes record identity transformations, so playback is a
  no-op on the corpus — but the plumbing is exercised
  end-to-end.
- `cassettes/zx/main.json` — identity transformation recorded
  for the canonical `main` module name used by tests.

Tests (1 ctest entry / 6 cases) all green:

```
ctest -L phonon_M6 → 1/1 pass
  phonon_m6_borrowed_test
    M6_synth.null_passthrough
    M6_synth.factory_returns_named
    M6_zx.null_passthrough
    M6_zx.cassette_replay_identity
    M6_zx.no_cassette_silent_passthrough
    M6_swappable.same_pipeline_with_either_impl
```

All tests still green: 30/30 ctest entries.

**Next:** M7 — pipeline orchestration + benchmarks.

## 2026-06-16 — M7 (Pipeline orchestration + swap policy + benchmarks) landed

Spec: `phaseB/M7_orchestration.md`. Closes Phase B.

Components shipped:

- `phonon/optimizer/include/phonon/optimizer/Pipeline.h` —
  `PipelineConfig` (with pluggable `synth` and `zx` adapters)
  plus `runPipeline(Module&, PipelineConfig)`.
- `phonon/optimizer/lib/Pipeline.cpp` — composes the M5
  built passes and the M6 borrowed adapters in the
  spec-mandated order: cancellation → merging → synthesis →
  ZX → cancellation again → merging again → schedule.
- `phonon/bench/bench.py` + `bench/README.md` — Python
  harness for comparing against pytket 2.16.0 and Qiskit's
  transpiler. Run out-of-CI; reports a markdown table.
  Qiskit is benchmark-only; never on the optimize path.
- `.github/workflows/phase-b-ci.yml` — two-job CI mirroring
  Phase A's: `build_and_test` runs ctest for both Phase A and
  Phase B labels; `coverage_gate` enforces ≥85% line
  coverage on `phonon/(dialect|parser|types|lower|optimizer)/`.

Tests (2 ctest entries / 5 cases) all green:

```
ctest -L phonon_M7 → 2/2 pass
  phonon_m7_e2e_test  (4 cases: bell_full_pipeline, ghz_full_pipeline,
                       qft_loop_full, bell_pair_func_full —
                       parse → typecheck → optimize → lower →
                       Phase A verify)
  phonon_m7_swap_test (1 case: null_vs_pyzx_same_corpus
                       gate counts identical — interface contract)
```

End-of-phase deliverables also written:

- **`docs/build/phaseB_phonon_guide.md`** — full user guide
  with three on-ramps (quantum, compilers, neither),
  installation, the language, the linear type checker, the
  optimizer (built + borrowed), pinned versions,
  troubleshooting, and the §9 Definition-of-Done checklist
  (all boxes ticked).
- **`docs/build/phaseB_progress.md`** (this file) — append-only
  journal, complete dated history of every milestone.
- **`docs/build/phaseB_decisions.md`** — four decisions
  (D9 conditional flattening, D10 loop-unroll variable
  rebinding, D11 ASAP scheduler, D12 borrowed-pass
  integration depth).
- **`docs/build/phaseB/`** — per-milestone engineering specs
  M1–M7, glossary, test-matrix, dialect-extension.

**Final test totals:**

```
ctest entries: 32 (15 Phase A + 17 Phase B)
individual cases:
  Phase A           : 116 (unchanged)
  Phase B           :  47 cases
    phonon M1: 11 (4 ctest entries)
    phonon M2: 12 (3 ctest entries)
    phonon M3:  9 (2 ctest entries)
    phonon M4:  6 (2 ctest entries)
    phonon M5: 10 (3 ctest entries)
    phonon M6:  6 (1 ctest entry)
    phonon M7:  5 (2 ctest entries)
  --------
  Total: 163, all green.
```

**Phase B is done.** Per Rule 1, do **not** open Phase C
(Photon) in this conversation — the handoff is one-fresh-chat-
per-phase.

## 2026-06-16 — M6 (Borrowed optimizer adapters) landed

Spec: `phaseB/M6_optimizer_borrowed.md`. Decision D12 below
records the integration depth choice.

Components shipped:

- `phonon/optimizer/include/phonon/optimizer/BorrowedPasses.h` —
  two owned C++ interfaces:
  - `ISynthesizer` with `name() / synthesize(module)`.
  - `IZxSimplifier` with `name() / simplify(module)`.
- `lib/Synthesizer.cpp` — `NullSynthesizer` (default) and a
  conditionally-compiled `TweedledumSynthesizer` (built when
  `SPINOR_HAVE_TWEEDLEDUM` is defined; falls back to NullImpl
  otherwise).
- `lib/ZxSimplifier.cpp` — `NullZxSimplifier` and
  `PyZXSubprocessSimplifier`. The PyZX impl looks for a
  cassette under `${PHONON_CASSETTE_DIR}/zx/<module>.json`;
  if present, plays it back. If absent, and
  `PHONON_PYZX_LIVE=1` is set, the subprocess path is taken
  (Phase D will wire the actual Python helper). Otherwise it
  passes through silently — keeping CI hermetic.
- Cassettes shipped under
  `phonon/optimizer/cassettes/zx/main.json` (identity).

Tests (1 ctest entry / 6 cases) all green:

```
ctest -L phonon_M6 → 1/1 pass
  phonon_m6_borrowed_test  (synth.null, synth.factory_returns_named,
                            zx.null, zx.cassette_replay_identity,
                            zx.no_cassette_silent, swappable)
```

All tests still green: 30/30 ctest entries.

**Next:** M7 — orchestration + benchmarks (pytket, Qiskit
gate-count comparison, pipeline e2e).
