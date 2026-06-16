# Phase B (Phonon) — test matrix

Single-page index of every Phase B test. Append a row whenever a
test lands. Match the milestone label exactly so `ctest -L M<n>`
selects the right group.

## Format

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |

`Type` is one of: unit, integration, property, regression,
golden, e2e.

---

## M1 — dialect

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M1_builder.bell_in_phonon` | unit | M1 | builder API | bell-state IR has 8 ops; types correct. |
| `M1_builder.const_and_binop` | unit | M1 | builder API | int/angle constants and binop produce well-typed results. |
| `M1_builder.def_call_return` | unit | M1 | builder API | def + call build, structural verifier accepts. |
| `M1_builder.teleportation_skeleton` | unit | M1 | builder API | Two `if` blocks pair correctly; structural verifier passes. |
| `M1_round_trip.bell_phonon_byte_equal` | integration | M1 | print → parse → print | byte equality. |
| `M1_round_trip.for_with_const_bounds` | integration | M1 | print → parse → print | byte equality. |
| `M1_verify.unpaired_if_rejected` | unit | M1 | hand-built bad IR | error mentions phonon.if. |
| `M1_verify.return_outside_def_rejected` | unit | M1 | hand-built bad IR | rejected. |
| `M1_verify.cmp_predicate_type` | unit | M1 | hand-built bad IR | predicate-type error. |
| `M1_verify.valid_program_passes` | unit | M1 | builder API | clean. |
| `M1_print_golden.bell_phonon` | golden | M1 | builder | matches `goldens/bell.phn`. |

## M2 — parser

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M2_contain.bell_spn` | integration | M2 | `spinor/tests/corpus/bell.spn` | parses through Phonon parser with no errors. |
| `M2_contain.ghz_spn` | integration | M2 | `spinor/tests/corpus/ghz.spn` | parses. |
| `M2_contain.rotations_spn` | integration | M2 | `spinor/tests/corpus/rotations.spn` | parses. |
| `M2_contain.native_ibm_spn` | integration | M2 | `spinor/tests/corpus/native_ibm.spn` | parses. |
| `M2_contain.native_ionq_spn` | integration | M2 | `spinor/tests/corpus/native_ionq.spn` | parses. |
| `M2_contain.mid_circuit_spn` | integration | M2 | `spinor/tests/corpus/mid_circuit.spn` | parses. |
| `M2_struct.bell_pair_func` | integration | M2 | `phonon/tests/corpus/bell_pair_func.phn` | one `phonon.def`, two `phonon.call`. |
| `M2_struct.qft_loop` | integration | M2 | `phonon/tests/corpus/qft_loop.phn` | one `phonon.for` / `phonon.end_for` pair. |
| `M2_struct.teleportation` | integration | M2 | `phonon/tests/corpus/teleportation.phn` | two `phonon.if`, two `phonon.cmp`. |
| `M2_error.unbalanced_brace` | unit | M2 | malformed | error reported. |
| `M2_error.unknown_func_handled` | unit | M2 | unknown function call | parser does not fail (resolution deferred). |
| `M2_error.missing_target_is_error` | unit | M2 | missing target | error reported. |

## M3 — linear type checker

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M3_linear.bell_passes` | unit | M3 | builder bell module | no errors. |
| `M3_linear.no_cloning_caught` | unit | M3 | hand-built bad IR | error E1 emitted. |
| `M3_linear.use_after_measure_caught` | unit | M3 | post-measure gate | E1 or E2 emitted. |
| `M3_linear.reset_after_measure_ok` | unit | M3 | reset between measure and gate | accepted. |
| `M3_linear.implicit_discard_warns` | unit | M3 | qubit unused | warning, not error. |
| `M3_linear.classical_no_op` | unit | M3 | int reused many times | no errors. |
| `M3_linear.teleportation_passes` | integration | M3 | parsed `teleportation.phn` | no errors. |
| `M3_linear.bell_pair_func_passes` | integration | M3 | parsed `bell_pair_func.phn` | no errors. |
| `M3_linear.qft_loop_passes` | integration | M3 | parsed `qft_loop.phn` | no errors. |

## M4 — lowering

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M4_lower.bell_passthrough` | integration | M4 | `bell.spn` parsed as Phonon | lowered op counts match. |
| `M4_lower.qft_loop_unrolls` | unit | M4 | `qft_loop.phn` | 4 `spinor.h` ops in output. |
| `M4_lower.bell_pair_func_inlines` | integration | M4 | `bell_pair_func.phn` | 2 H, 2 CX, 4 measure in output. |
| `M4_lower.no_phonon_ops_in_output` | property | M4 | `teleportation.phn` | output is non-empty (and Spinor-only by type). |
| `M4_pipeline.bell_through_phase_a_verify` | integration | M4 | parsed bell → lowered | `spinor::verify::verify` accepts. |
| `M4_pipeline.qft_loop_through_phase_a_verify` | integration | M4 | parsed qft_loop → lowered | `spinor::verify::verify` accepts. |

## M5 — built optimizer

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M5_cancel.h_h_cancels` | unit | M5 | builder `H; H` | output empty (alloc only). |
| `M5_cancel.x_x_cancels` | unit | M5 | builder `X; X` | output empty. |
| `M5_cancel.s_sdg_cancels` | unit | M5 | builder `S; Sdg` | output empty. |
| `M5_cancel.cx_cx_cancels` | unit | M5 | builder `CX; CX` | output empty. |
| `M5_cancel.no_false_cancel_when_op_between` | unit | M5 | `H; X; H` | both Hs remain. |
| `M5_merge.rz_rz_drops_to_zero` | unit | M5 | `Rz(0.5); Rz(-0.5)` | output drops both. |
| `M5_merge.rz_rz_fuses` | unit | M5 | `Rz(0.4); Rz(0.3)` | one Rz remains. |
| `M5_merge.rx_rx_fuses` | unit | M5 | `Rx(1.0); Rx(0.5)` | one Rx remains. |
| `M5_pipeline.bell_unchanged` | integration | M5 | bell.spn parsed | gate counts unchanged. |
| `M5_pipeline.redundant_h_pair_shrinks` | integration | M5 | `H; H` | gate count strictly decreases. |

## M6 — borrowed adapters

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M6_synth.null_passthrough` | unit | M6 | bell module | NullImpl returns unchanged. |
| `M6_synth.factory_returns_named` | unit | M6 | factory | returns "null" or "tweedledum". |
| `M6_zx.null_passthrough` | unit | M6 | bell module | NullImpl returns unchanged. |
| `M6_zx.cassette_replay_identity` | integration | M6 | bell module + main.json cassette | playback no-op (Phase B identity cassette). |
| `M6_zx.no_cassette_silent_passthrough` | unit | M6 | unknown module name | silent passthrough; module unchanged. |
| `M6_swappable.same_pipeline_with_either_impl` | property | M6 | two pipeline runs | gate counts identical (interface contract). |

## M7 — orchestration

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M7_e2e.bell_full_pipeline` | e2e | M7 | `bell.spn` parsed as Phonon | full pipeline → Phase A verifier accepts. |
| `M7_e2e.ghz_full_pipeline` | e2e | M7 | `ghz.spn` parsed as Phonon | full pipeline → Phase A verifier accepts. |
| `M7_e2e.qft_loop_full` | e2e | M7 | `qft_loop.phn` | full pipeline → Phase A verifier accepts. |
| `M7_e2e.bell_pair_func_full` | e2e | M7 | `bell_pair_func.phn` | full pipeline → Phase A verifier accepts. |
| `M7_swap.null_vs_pyzx_same_corpus` | property | M7 | bell module + each impl | gate counts identical (Phase B identity-cassette). |

## M6 — borrowed optimizer adapters

| Test name | Type | Milestone | Inputs | Asserts |
| --- | --- | --- | --- | --- |
| `M6_synth.null_passthrough` | unit | M6 | bell module | NullImpl unchanged. |
| `M6_synth.factory_returns_named` | unit | M6 | factory | name == "null" or "tweedledum". |
| `M6_zx.null_passthrough` | unit | M6 | bell module | NullImpl unchanged. |
| `M6_zx.cassette_replay_identity` | integration | M6 | bell + cassette | unchanged. |
| `M6_zx.no_cassette_silent_passthrough` | unit | M6 | unknown module | passthrough; no error. |
| `M6_swappable.same_pipeline_with_either_impl` | property | M6 | two impls run on identical input | structurally identical output. |
