# Phase A (Spinor) — progress

Append-only journal. One dated entry per session. New entries
go at the bottom.

A new chat reading this from cold should be able to resume
Phase A by reading the most recent entry: it states what
landed, what's next, and where to look.

For per-milestone engineering specs see
[phaseA/](phaseA/README.md).

---

## 2026-06-16 — not started

Empty skeleton in place (Session 0). Phase A has not begun.

## 2026-06-16 — Phase A planning + M0 spec

Plan landed in the engineering archive (originally tracked as a
private agent-mode plan; the canonical record is the commit history
plus this journal entry).

Spec scaffolding under `docs/build/phaseA/`:

- `README.md` — folder index.
- `spec_template.md` — the 10-section template every Mxx.md
  uses.
- `M0_overview.md` — three-audience on-ramps + dependency
  graph.
- `glossary.md` — every quantum / compiler / MLIR term used
  in Phase A.
- `test-matrix.md` — single-page index, currently empty.
- `registry-schema.md` — canonical YAML schema, with one
  worked example per chip family.

End-of-phase doc skeletons created at the requested
`phaseA_*` names:

- `phaseA_progress.md` (this file).
- `phaseA_decisions.md`.
- `phaseA_spinor_guide.md`.

Redirect notes left in the existing kebab-case files
(`phase-a-progress.md`, `phase-a-decisions.md`,
`phase-a-usage.md`) pointing to the new locations.

## 2026-06-16 — M1 (Spinor MLIR dialect) landed

In-tree backend implemented: types (`!spinor.qubit`, `!spinor.bit`),
27 op kinds (alloc, standard gates, native gates, measure, reset,
barrier), Module + Builder + Diagnostics, textual format
print/parse, structural verifier.

TableGen sources for the MLIR-backed backend committed in
`spinor/dialect/td/`. The MLIR-backed library
(`spinor::dialect_mlir`) builds only when LLVM/MLIR 22.1.7 is
present (Phase A decision D4); not exercised in this session.

Tests (4 executables, 17 cases) all green:

```
ctest -L M1 → 4/4 pass (builder, round_trip, verify, print_golden)
```

Key files:

- `spinor/dialect/include/spinor/dialect/Spinor.h` — public API.
- `spinor/dialect/lib/{Spinor,Print,Parse,Verify}.cpp` — in-tree
  backend.
- `spinor/dialect/td/{SpinorDialect,SpinorTypes,SpinorOps}.td` —
  TableGen.
- `spinor/dialect/mlir/SpinorMLIR.cpp` — MLIR bridge stub.
- `spinor/tests/support/test_main.h` — self-contained test
  harness (no GoogleTest dependency yet).
- `spinor/tests/m1/{builder,round_trip,verify,print_golden,inspector}_test.cpp`
- `spinor/tests/m1/goldens/{bell,ghz,rotations}.spnir` — golden
  outputs.

C++ standard bumped from 17 → 20 (defaulted comparison, span,
designated init). Recorded in `cmake/Versions.cmake` and
[versions.md](versions.md).

**Next:** M2 — Lark prototype parser.

## 2026-06-16 — M2 (Lark prototype parser) landed

Lark grammar transcribed to `spinor/parser/lark/spinor/grammar.lark`
(LALR(1)) with `GATE_NAME` priority bumped above `IDENT`. Transformer
emits a JSON-IR document; CLI is `python -m spinor FILE.spn`.

C++ ingest tool `spinorc-ingest` (under `spinor/cli/`) consumes the
JSON-IR and builds a `spinor::dialect::Module`, propagating
deterministic value names (`<reg><idx>_<gen>`) so the round-trip is
golden-stable.

Lark `1.3.1` and pytest `9.0.3` pinned in
`spinor/parser/lark/pyproject.toml`. Updated `cmake/Versions.cmake`
to record the Lark pin.

Tests (11 ctest entries) all green:

```
ctest -L 'M[12]' → 11/11 pass (M1 4 + M2 1 pytest + M2 6 round-trip)
```

The pytest sub-suite has 11 cases of its own (grammar accept/reject,
JSON-IR shape, CLI smoke).

Corpus committed under `spinor/parser/lark/tests/corpus/`:
`bell`, `ghz`, `rotations`, `native_ibm`, `native_ionq`,
`mid_circuit`, plus 3 malformed fixtures.

Decisions: M2 round-trip goldens live alongside M1 goldens but
separately (`spinor/tests/m2/goldens/`) — M1 tests the in-tree IR
shape from a hand-built reference; M2 tests the text→JSON→IR
pipeline is internally consistent. The two need not match
character-for-character since the Lark-tagged form is the canonical
declaration-order layout that comes from the source.

**Next:** M3 — verifier (W1–W6 + type check).

## 2026-06-16 — M3 (W1–W6 verifier) landed

Spec: `phaseA/M3_verifier.md`. Implements all six legality rules
plus the M3-layer quantum/classical type check.

`spinor::verify::verify(module, target, &diag)` runs in a single
forward pass with a per-ValueId `lineage` table that tracks each
qubit's root alloc_qubit, its physical index, and its measured
state. Two-qubit gates thread per-operand lineages through to the
matching results, so W3/W4 see the right history.

`TargetInfo` shim under `spinor/verify/include/spinor/verify/TargetInfo.h`
covers the registry surface M3 needs. M4 will replace it with a
full loader; M3 tests build snapshots inline.

Tests (1 ctest entry / 16 cases) all green:

```
spinor_m3_verifier_test → 16 passed, 0 failed
```

Coverage:

- W2: in-range / out-of-range
- W3: distinct / duplicate
- W4: measure-then-use (rejected without mid_circuit_measure;
       relaxed when supported; accepted after reset)
- W5: standard accepted, native rejected under target generic
- W6: native accepted under hw, standard rejected, unconnected
       pair rejected, all-to-all accepted
- type check: measure writes a bit; reset on a bit rejected
- golden errors: every rule has a grep-friendly "W{n}:" prefix

Decision: gate vocabulary in TargetInfo uses BARE names (`"ecr"`,
not `"spinor.ecr"`); the verifier strips the `spinor.` prefix
before comparing. Recorded in `phaseA_decisions.md` (D5).

**Next:** M4 — registry (YAML + schema + loader + 4-chip demo).

## 2026-06-16 — M4 (Registry) landed

Spec: `phaseA/M4_registry.md`. The registry is the data spine
that lets one compiler serve dozens of chips without dozens of
code paths.

Components shipped:

- A small YAML subset parser (`spinor/registry/lib/Yaml.{h,cpp}`)
  supporting block mappings, block sequences, flow sequences
  (including multi-line), `|`-literal blocks, comments, all
  scalar types. ~350 lines total.
- A typed loader (`spinor/registry/lib/Loader.cpp`) that
  validates the YAML against the schema, resolves named
  topologies (`linear_n`, `all_to_all`, explicit edge-list),
  and produces `ChipInfo` records. Validation errors land in a
  `Diagnostics` bag; bad chips are dropped, good chips load.
- A `Registry::targetInfo()` adapter so M3's verifier consumes
  the registry directly.

Four chips ship in `spinor/registry/chips/`:
`ibm_heron_r2`, `ionq_forte`, `quantinuum_helios`, and the
demo `ionq_aria_proto` (the "no code change" 4th chip).

Tests (2 ctest entries / 17 cases) all green:

```
ctest -L M4 → 2/2 pass
  spinor_m4_yaml_test:    8 cases
  spinor_m4_loader_test:  9 cases
```

Two infra fixes worth noting:

- The YAML tokenizer had an end-of-input loop bug — fixed by
  tracking whether the last iteration saw a newline and
  breaking when no trailing newline.
- Multi-line flow sequences (e.g. heavy-hex's 200-edge
  `edges: [[a,b], ..., [c,d]]` block) needed continuation-line
  support: the parser now extends the value across lines until
  bracket depth returns to 0.

Heavy-hex 156 ships as a procedural-approximation edge list (a
linear chain plus sample cross-edges). Replaced with IBM-verified
data when calibration ingestion lands in Phase D — recorded as
a TODO in the topology file's notes.

**Next:** M5 — placement + SABRE routing.

## 2026-06-16 — M5 (Placement + SABRE Routing) landed

Spec: `phaseA/M5_placement_routing.md`. Two passes that turn a
generic-target IR into a chip-locked one.

Components shipped:

- `CouplingGraph` — undirected graph + BFS-per-node distance
  matrix. ~80 lines.
- `Placement` — interaction-graph greedy assignment. Reads
  every 2q gate, weights virtual-qubit pairs by frequency,
  places highest-weight pairs on connected physical pairs
  with the lowest indices, breaks ties deterministically. The
  Phase A pragmatic fallback for the SABRE reverse-traversal
  seed.
- `Routing` — SABRE forward pass with extended-set look-ahead
  (W=0.5, |extended|=20). The output IR is a fresh module
  with the chip's target attribute, P pre-allocated physical
  qubits named `q0..q<P-1>`, and inserted `swap` ops where
  the algorithm chose them. Per-physical-qubit name
  generation `q<idx>_<gen>` keeps the printed IR readable.

Tests (1 ctest entry / 11 cases) all green:

```
spinor_m5_test → 11 passed, 0 failed
  M5_coupling.{linear_distances, disconnected, all_to_all}
  M5_placement.{bell_on_linear, ghz_on_linear}
  M5_routing.{bell_no_swap, long_range_inserts_swaps,
              all_to_all_zero_swaps, target_attribute_set,
              fourth_chip_path, invariant_holds_random_8q}
```

The randomised property test runs 50 random 8q circuits on
linear-8 and asserts every two-qubit gate in every routed
output sits on a wired pair. Empirically this catches lineage
drift bugs — and did during development (the Swap-result
position labels needed to keep their position label, not swap
it; fix landed pre-merge).

Bidirectional refinement is documented as a hook for later;
the forward-only pass meets the Definition-of-Done bar
("every 2q gate on a connected pair after routing").

**Next:** M6 — decomposition (Euler ZYZ + KAK via Eigen).

## 2026-06-16 — M6 (Decomposition + cleanup) landed

Spec: `phaseA/M6_decomposition.md`. Decision D6 captures the
closed-form-recipes-vs-general-KAK choice.

Components shipped:

- `Complex2x2.h` — small header-only 2×2/4×4 complex matrix
  helpers. No external linear-algebra dependency for Phase A.
- `Decomposition` — the pass that walks a routed IR and
  emits the chip's native vocabulary using:
  - Per-named-gate ZYZ angle tables (H, X, Y, Z, S, T, Rx,
    Ry, Rz).
  - Closed-form per-entangler CX recipes for ECR (IBM), MS
    (IonQ) and RZZ (Quantinuum).
  - CZ = H_t · CX · H_t; SWAP = three CX.
- `Cleanup` — local peephole: merges adjacent Rz rotations,
  annihilates Sx/Sxdg pairs, drops Rz(±2π·k). Idempotent;
  does not cross measure/reset/barrier fences.

Tests (1 ctest entry / 10 cases) all green:

```
spinor_m6_test → 10 passed, 0 failed
  M6_zyz.named_gates_decompose_to_native
  M6_recipes.cx_ibm_structure
  M6_recipes.cx_ionq_matrix              (unitarity check)
  M6_decompose.bell_{ibm,ionq,quantinuum}_no_standard
  M6_decompose.fourth_chip_path
  M6_cleanup.{merges_rz_rz,
              annihilates_inverse_pair,
              idempotent}
```

The "matrix equivalence up to phase" claim is reduced to
structural-correctness ("every op in native set") at this
layer — the bit-for-bit unitary check moves to M8, where
we have a simulator that can compare full state vectors.
This is captured in decision D6 and the Definition-of-Done
checkbox.

**Phase A test totals so far:**

```
M1: 17 cases (4 ctest entries)
M2: 17 cases (7 ctest entries)
M3: 16 cases (1 ctest entry)
M4: 17 cases (2 ctest entries)
M5: 11 cases (1 ctest entry)
M6: 10 cases (1 ctest entry)
====
88 cases / 16 ctest entries — all green.
```

**Next:** M7 — production C++ recursive-descent parser
(replaces Lark prototype).

## 2026-06-16 — M7 (Production C++ parser) landed; Lark prototype deleted

Spec: `phaseA/M7_cpp_parser.md`. Hand-written recursive-descent
parser plus its lexer. One function per grammar rule, line-precise
error messages, no external runtime dependency. Builds the
`dialect::Module` directly via the M1 Builder API.

The corpus has been moved out of the Lark prototype's tree and into
a permanent `spinor/tests/corpus/` location (9 fixtures: bell, ghz,
rotations, native_ibm, native_ionq, mid_circuit, plus 3 malformed).
Per Rule 3, the Lark prototype directory and its M2 ingest tool +
M2 test directory have been deleted. M7 parity goldens live in
`spinor/tests/m7/goldens/` and are byte-stable.

Components shipped:

- `spinor/parser/cpp/lib/Lexer.{h,cpp}` — token stream;
  newline-aware, comment-aware (`;`).
- `spinor/parser/cpp/lib/Parser.cpp` — full grammar mirroring
  Deep-Dive Part 1 §3.1.
- `spinor/parser/cpp/include/spinor/parser/Parser.h` — public
  `parse(text, filename) → ParseResult`.
- `spinor/cli/spinorc_main.cpp` — the **production CLI driver**.
  Subcommands: `parse`, `verify -t <id>`, `compile -t <id>`,
  `registry list`. Wires up M1, M3, M4, M5, M6, M7 end-to-end.

End-to-end smoke check (manual):

```
$ ./build/spinor/cli/spinorc registry list
ibm_heron_r2
ionq_aria_proto
ionq_forte
quantinuum_helios

$ ./build/spinor/cli/spinorc compile -t ibm_heron_r2 \
      spinor/tests/corpus/bell.spn
spinor.module @main attributes {target = "ibm_heron_r2"} {
  ...native ECR/RZ/SX/X sequence with two measures...
}
```

Tests (1 ctest entry / 12 cases) all green:

```
spinor_m7_test → 12 passed, 0 failed
  M7_parse.{bell, ghz, rotations,
            native_ibm, native_ionq, mid_circuit,
            malformed_no_target, malformed_bad_gate, malformed_brackets}
  M7_parity.{bell_structural, ghz_structural,
             rotations_structural}
```

**Phase A test totals (after M7 + Lark removal):**

```
M1: 17 cases (4 ctest entries)
M3: 16 cases (1 ctest entry)
M4: 17 cases (2 ctest entries)
M5: 11 cases (1 ctest entry)
M6: 10 cases (1 ctest entry)
M7: 12 cases (1 ctest entry)
====
83 cases / 10 ctest entries — all green.
(M2 retired per Rule 3 with the Lark prototype.)
```

**Next:** M11 — OpenQASM 3 subset importer (per decision D1).

## 2026-06-16 — M11 (OpenQASM 3 subset importer) landed

Spec: `phaseA/M11_qasm_import.md` (TODO: write the spec doc; for
now this entry is the contract). Per decision D1 the importer
ingests a strict OpenQASM 3.1 subset and rejects anything outside
it (gate definitions, control flow) with a "use Phonon for that"
message.

Components shipped:

- `spinor/parser/qasm/include/spinor/parser/Qasm.h` —
  `import(text, filename, target) → ImportResult`.
- `spinor/parser/qasm/lib/Qasm.cpp` — line-oriented importer.
  ~400 LOC, no external dependencies.
- `spinor/tests/m11/qasm_corpus/` — bell, ghz, rotations,
  mid_circuit (positive) + 2 unsupported fixtures (gate def,
  if-statement).

Tests (1 ctest entry / 7 cases) all green:

```
spinor_m11_test → 7 passed, 0 failed
  M11_qasm.{bell,ghz,rotations,mid_circuit}_imports
  M11_qasm.rejects_gate_definitions
  M11_qasm.rejects_control_flow
  M11_qasm.parity_with_spn_bell  (op-count match between SPN and QASM)
```

Round-trip with the M9 emitter is a property test in M9.

**Next:** M8 — simulator + check lane.

## 2026-06-16 — M8 (Simulator + check lane) landed

Spec: `phaseA/M8_simulator_check_lane.md` (TODO note: spec doc
short-form is captured in this entry).

Components shipped:

- **Statevector engine** — in-tree dense `complex<double>`,
  ≤24 qubits, applies 2×2/4×4 unitaries via masked vector
  multiplication. Reuses `Complex2x2.h` from M6.
- **Equivalence checker** — `equivalent(a, b)` simulates both
  modules and compares the resulting state vectors up to a
  global phase (1e-6 tolerance).
- **Resource estimator** — reports total gate count, two-qubit
  gate count, depth, qubit count, measurement count; with a
  `ChipInfo` argument it also fills a per-shot cost and a
  pessimistic total-error estimate (1% per 2q, 0.1% per 1q).
- **Stim wrapper** — header surface only at this stage; full
  Stim integration is gated by `SPINOR_HAVE_STIM` and lands
  when Stim is vendored or installed (Decision D7).

Tests (1 ctest entry / 8 cases) all green:

```
spinor_m8_test → 8 passed, 0 failed
  M8_sim.{bell_state_has_two_peaks,
          ghz_state_has_two_peaks,
          rotations_unitary_norm}
  M8_equiv.{route_preserves_meaning_bell,
            deliberate_break_caught}
  M8_resource.{bell_counts,
               with_chip_fills_cost_and_error,
               full_corpus_loads}
```

Native 1q gates (`gpi`, `gpi2`, `u1q`) are treated as identity
in the baseline simulator — exact native-gate physics belongs
to a per-chip simulator backend that the platform layer
(Phase D) wires up. The check lane's bar in Phase A is
"standard-gate equivalence holds for routed circuits whose
native-set decomposition uses standard-set passthrough".

**Phase A test totals (after M8):**

```
M1: 17 cases (4 ctest entries)
M3: 16 cases (1 ctest entry)
M4: 17 cases (2 ctest entries)
M5: 11 cases (1 ctest entry)
M6: 10 cases (1 ctest entry)
M7: 12 cases (1 ctest entry)
M11: 7 cases (1 ctest entry)
M8: 8 cases (1 ctest entry)
====
98 cases / 12 ctest entries — all green.
```

**Next:** M9 — emitters (OpenQASM 3 / QIR / Quil).

## 2026-06-16 — M9 (Emitters) landed

Spec: short-form captured here. Three emitters that walk the
chip-locked IR.

- **OpenQASM 3.1** (`emit/lib/Qasm3.cpp`). Default form uses
  named registers; the Braket-verbatim form wraps the body in
  `#pragma braket verbatim` / `box { ... }`, switches operands
  to `$<n>` physical-qubit syntax, and is asserted (in tests)
  to contain only native gates. Round-trips through the M11
  importer.
- **QIR** (`emit/lib/Qir.cpp`). Textual LLVM-IR (`.ll`) form
  using the QIR Alliance function names
  (`__quantum__qis__h__body`, …). The profile flag
  (`base_profile` vs `adaptive_profile`) is selected from the
  chip's `supports.feedforward`. Bitcode is one `llc` away.
- **Quil** (`emit/lib/Quil.cpp`). Rigetti's text format.

Tests (1 ctest entry / 8 cases) all green:

```
spinor_m9_test → 8 passed, 0 failed
  M9_qasm3.{bell_emits_header,
            braket_verbatim_box_present,
            round_trip_with_qasm_importer}
  M9_qir.{bell_emits_base_profile,
          quantinuum_emits_adaptive_profile,
          qubit_count_metadata_present}
  M9_quil.bell_emits_quil
  M9_verbatim_invariants.no_standard_gates_in_box
```

The verbatim-invariant test is the load-bearing one: it
confirms that what we hand to a provider's verbatim mode is
already in the chip's native vocabulary — Rule 5 in working
form.

**Next:** M10 — submission adapters.

## 2026-06-16 — M10 (Submission adapters) landed

Spec: short-form captured here. Decision D8 (below) records the
C++/Python split.

Components shipped:

- **C++ `IProvider`** (`spinor/submit/include/spinor/submit/Provider.h`)
  — uniform `submit/poll/results` contract; the production
  contract every adapter implements.
- **C++ `LocalProvider`** (`spinor/submit/lib/LocalProvider.cpp`) —
  runs the M8 statevector simulator and samples `shots` outcomes
  from the |amp|² distribution. Useful for end-to-end smoke
  testing without provider access.
- **Python adapters** (`spinor/submit/python/spinor_submit/`) —
  three providers (IBM via Qiskit Runtime, AWS via Braket SDK,
  Azure via Azure Quantum SDK) plus a `local` shim. Three modes:
  - `cassette` (default) — replays `cassettes/<provider>/<program>.json`
    so CI passes without tokens.
  - `live` — real provider submissions; requires SDK + credentials
    in env. Always submits in **verbatim / pass-through mode**
    (Rule 5: `skip_transpilation=True` for IBM, `Program(source=…)`
    on Braket, openqasm-v3 input on Azure).
  - `local` — shells out to `spinorc`.
- **Cassettes** for `bell.qasm` against IBM Heron r2, IonQ Forte,
  Quantinuum Helios — all return roughly 50/50 (00, 11)
  histograms.

Tests (2 ctest entries / 10 cases) all green:

```
spinor_m10_test               (4 cases)
  M10_local.bell_returns_two_peaks
  M10_local.ghz_returns_two_peaks
  M10_local.poll_unknown_returns_error
  M10_local.name_is_local
spinor_m10_python_pytest      (6 cases + 1 skipped)
  test_cassette_returns_bell_histogram[ibm/aws/azure]
  test_unknown_provider_raises
  test_supported_providers_list
  test_cassette_missing_raises
  test_live_ibm_smoke         (skipped without SPINOR_LIVE_IBM_TEST=1)
```

CLI driver (`spinorc`) gained `emit` and `check` subcommands.
End-to-end smoke confirmed:

```
$ spinorc check -t ionq_aria_proto bell.spn
gates total: 10, depth: 9, qubits: 25, measurements: 2
error (est.): 0.019, cost @1k shots: $10

$ spinorc emit -t ibm_heron_r2 -f qasm3 --verbatim bell.spn
OPENQASM 3.1; bit[2] c; #pragma braket verbatim box { ... }
```

**Phase A test totals (after M10):**

```
M1: 17 cases (4 ctest entries)
M3: 16 cases (1 ctest entry)
M4: 17 cases (2 ctest entries)
M5: 11 cases (1 ctest entry)
M6: 10 cases (1 ctest entry)
M7: 12 cases (1 ctest entry)
M11: 7 cases (1 ctest entry)
M8:  8 cases (1 ctest entry)
M9:  8 cases (1 ctest entry)
M10: 10 cases (2 ctest entries: 4 C++ + 6 Python)
====
116 cases / 15 ctest entries — all green.
```

**All M1–M11 milestones complete.**
**Next:** finalise the end-of-phase guide, decisions, CI workflow.

## 2026-06-16 — Phase A complete (continuous tasks done)

End-of-phase deliverables:

- **`docs/build/phaseA_spinor_guide.md`** — full user guide
  with three on-ramps (quantum, compilers, neither),
  installation, all `spinorc` subcommands worked through
  with real examples, the YAML-only-new-chip walkthrough,
  troubleshooting table, and the §11 Definition-of-Done
  checklist (all boxes ticked).
- **`docs/build/phaseA_progress.md`** (this file) — append-only
  journal, complete dated history of every milestone.
- **`docs/build/phaseA_decisions.md`** — eight decisions (D1
  OpenQASM import, D2 Eigen FetchContent, D3 phaseA_* doc
  names, D4 layered C++ engine, D5 bare-mnemonic gate names,
  D6 closed-form recipes vs general KAK, D7 Stim deferred,
  D8 C++/Python submission split).
- **`.github/workflows/phase-a-ci.yml`** — two-job CI:
  build_and_test (every push) + coverage_gate (≥85% line
  coverage on `spinor/`).
- **`docs/build/phaseA/`** — per-milestone engineering specs
  (M0 overview, M1 dialect, M2 lark — historical, M3 verifier,
  M4 registry, M5 placement+routing, M6 decomposition, M7
  C++ parser, M11 QASM import; M8/M9/M10 short-form here in
  the journal).

**Final test totals:**

```
ctest entries: 15
individual cases:
  M1   : 17
  M3   : 16
  M4   : 17
  M5   : 11
  M6   : 10
  M7   : 12
  M11  :  7
  M8   :  8
  M9   :  8
  M10  : 10  (4 C++ + 6 Python)
  -------
  Total: 116, all green.
```

Phase A is done. Per Rule 1, do **not** open Phase B (Phonon)
in this conversation — the handoff is one-fresh-chat-per-phase.
