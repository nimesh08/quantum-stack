# Phase A — Test Matrix

Single-page index of every test in Phase A, kept current as
milestones land. If a test exists but isn't listed here, the
milestone is incomplete.

Columns:

- **ID** — `Mxx-Tnn` where `xx` is the milestone and `nn` is a
  per-milestone counter.
- **Name** — the CTest / pytest test name.
- **Type** — `unit`, `integration`, `property`, `fuzz`,
  `regression`, `golden`, or `e2e`.
- **What it asserts** — one sentence.
- **CI gate** — the GitHub Actions job that must run it.

| ID  | Name | Type | What it asserts | CI gate |
| --- | ---- | ---- | --------------- | ------- |
| M1-T01 | `M1_builder.bell_module` | unit | Bell-program Builder API produces 8 ops, target=generic, 9 values | `ctest -L M1` |
| M1-T02 | `M1_builder.value_uniqueness` | property | every produced ValueId is unique across 1000 random builder traces | `ctest -L M1` |
| M1-T03 | `M1_builder.type_signatures` | unit | gate ops produce the right Qubit/Bit types per signature | `ctest -L M1` |
| M1-T04 | `M1_print.bell_matches_golden` | golden | printed Bell module matches `goldens/bell.spnir` byte-for-byte | `ctest -L M1` |
| M1-T05 | `M1_print.ghz_matches_golden` | golden | printed GHZ module matches `goldens/ghz.spnir` | `ctest -L M1` |
| M1-T06 | `M1_print.rotations_matches_golden` | golden | printed rotations module matches `goldens/rotations.spnir` | `ctest -L M1` |
| M1-T07 | `M1_round_trip.bell` | property | `print → parse → print` is identity on the Bell module | `ctest -L M1` |
| M1-T08 | `M1_round_trip.ghz` | property | `print → parse → print` is identity on GHZ | `ctest -L M1` |
| M1-T09 | `M1_round_trip.rotations` | property | `print → parse → print` is identity on rotations | `ctest -L M1` |
| M1-T10 | `M1_round_trip.fuzz` | fuzz | `print → parse → print` is identity on 200 random valid IRs | `ctest -L M1` |
| M1-T11 | `M1_verify.accept_well_formed_bell` | unit | structural verifier returns no errors on Bell | `ctest -L M1` |
| M1-T11b | `M1_verify.accept_well_formed_rotations` | unit | structural verifier returns no errors on rotations | `ctest -L M1` |
| M1-T12 | `M1_verify.reject_undefined_operand` | unit | verifier flags an op referencing an undefined ValueId | `ctest -L M1` |
| M1-T13 | `M1_verify.reject_type_mismatch` | unit | verifier flags `h` consuming a `!spinor.bit` | `ctest -L M1` |
| M1-T14 | `M1_verify.reject_missing_attribute` | unit | verifier flags `rz` without an `angle` attribute | `ctest -L M1` |
| M1-T15 | `M1_verify.reject_no_target` | unit | verifier flags an empty `target` attribute | `ctest -L M1` |
| M1-T16 | `M1_verify.reject_duplicate_result_id` | unit | verifier flags duplicated result ids | `ctest -L M1` |
| M1-T17 | mlir_backend.dialect_registers | unit | (LLVM build only) MLIRContext loads `spinor` dialect; module verifies | `ctest -L M1 -L MLIR` |
| M1-T18 | mlir_backend.bell_lowers_and_back | integration | (LLVM build only) in-tree → MLIR → in-tree is identity on Bell | `ctest -L M1 -L MLIR` |
| M2-T01 | `test_grammar_bell_parses` | unit | Lark parser produces 8 ops for Bell with correct kinds and writes-back operand | `ctest -L M2` |
| M2-T02 | `test_grammar_ghz_parses` | unit | Lark parser produces 12 ops for GHZ with declarations-then-gates ordering | `ctest -L M2` |
| M2-T03 | `test_grammar_rotations_parses` | unit | `pi/2`, `-pi`, `0.5*pi` evaluate to expected numeric angles | `ctest -L M2` |
| M2-T04 | `test_grammar_native_ibm_parses` | unit | `ecr`, `sx`, `rz` accepted under `target ibm_heron_r2` | `ctest -L M2` |
| M2-T05 | `test_grammar_native_ionq_parses` | unit | `ms`, `gpi`, `gpi2` accepted under `target ionq_forte` | `ctest -L M2` |
| M2-T06 | `test_grammar_mid_circuit_parses` | unit | mid-stream `measure` and `reset` parse cleanly | `ctest -L M2` |
| M2-T07 | `test_grammar_malformed_no_target` | regression | parser rejects file lacking `target` declaration | `ctest -L M2` |
| M2-T08 | `test_grammar_malformed_bad_gate` | regression | parser rejects `hadamard q[0]` (unknown gate) | `ctest -L M2` |
| M2-T09 | `test_grammar_malformed_unbalanced_bracket` | regression | parser rejects `h q[0` (missing `]`) | `ctest -L M2` |
| M2-T-json | `test_json_ir_well_formed` | unit | JSON-IR has expected target / regs / ops shape | `ctest -L M2` |
| M2-T-cli | `test_cli_smoke` | e2e | `python -m spinor bell.spn` exits 0; output is valid JSON | `ctest -L M2` |
| M2-T10 | `spinor_m2_round_trip_bell` | integration | text → JSON → ingest → printed IR matches goldens/bell | `ctest -L M2` |
| M2-T11 | `spinor_m2_round_trip_ghz` | integration | round-trip identity on GHZ | `ctest -L M2` |
| M2-T12 | `spinor_m2_round_trip_rotations` | integration | round-trip identity on rotations (with `pi/2`, `-pi`, `0.5*pi`) | `ctest -L M2` |
| M2-T13 | `spinor_m2_round_trip_native_ibm` | integration | round-trip identity on native IBM gates | `ctest -L M2` |
| M2-T14 | `spinor_m2_round_trip_native_ionq` | integration | round-trip identity on native IonQ gates | `ctest -L M2` |
| M2-T15 | `spinor_m2_round_trip_mid_circuit` | integration | round-trip identity on mid-circuit measure+reset | `ctest -L M2` |
| M3-T01 | `M3_w2.in_range_accept` | unit | hw target with N qubits accepts ops on q[0..N-1] | `ctest -L M3` |
| M3-T02 | `M3_w2.out_of_range_reject` | unit | error: "qubit index … out of range" | `ctest -L M3` |
| M3-T03 | `M3_w3.distinct_accept` | unit | `cx q[0], q[1]` accepted | `ctest -L M3` |
| M3-T04 | `M3_w3.duplicate_reject` | unit | `cx q[0], q[0]` → "W3 distinct operands" | `ctest -L M3` |
| M3-T05 | `M3_w4.measure_then_use_reject` | unit | gate after measure on chip without mid-circuit fails | `ctest -L M3` |
| M3-T06 | `M3_w4.measure_then_reset_then_use_accept` | unit | reset clears measured state | `ctest -L M3` |
| M3-T07 | `M3_w4.mid_circuit_relaxes` | unit | chip with mid_circuit_measure=true allows post-measure ops | `ctest -L M3` |
| M3-T08 | `M3_w5.standard_only_accept` | unit | `target generic`; standard gates pass | `ctest -L M3` |
| M3-T09 | `M3_w5.native_under_generic_reject` | unit | `target generic`; `ecr` rejected | `ctest -L M3` |
| M3-T10 | `M3_w6.native_under_hw_accept` | unit | `target ibm_heron_r2`; `ecr` on connected pair passes | `ctest -L M3` |
| M3-T11 | `M3_w6.standard_under_hw_reject` | unit | `target ibm_heron_r2`; `cx` rejected as not native | `ctest -L M3` |
| M3-T12 | `M3_w6.unconnected_pair_reject` | unit | `ecr q[0], q[3]` on linear-4 → "connected pair" | `ctest -L M3` |
| M3-T13 | `M3_w6.all_to_all_accept` | unit | trapped-ion all-to-all accepts any pair | `ctest -L M3` |
| M3-T14 | `M3_typecheck.measure_writes_a_bit` | unit | `measure` produces a `!spinor.bit` value | `ctest -L M3` |
| M3-T15 | `M3_typecheck.reset_on_bit_rejected` | unit | hand-corrupted reset on a bit → type error | `ctest -L M3` |
| M3-T16 | `M3_golden_errors.w_codes_grep_friendly` | golden | every rule's error string contains "W{n}:" | `ctest -L M3` |
| M4-T01 | `M4_yaml.parse_scalars` | unit | YAML int / double / quoted string | `ctest -L M4` |
| M4-T02 | `M4_yaml.parse_block_mapping` | unit | nested key/value blocks | `ctest -L M4` |
| M4-T03 | `M4_yaml.parse_block_sequence` | unit | `-` items at proper indent | `ctest -L M4` |
| M4-T04 | `M4_yaml.parse_flow_sequence` | unit | `[a, b, c]` flow form | `ctest -L M4` |
| M4-T04b | `M4_yaml.parse_nested_flow` | unit | `[[0,1],[1,2]]` nested flow | `ctest -L M4` |
| M4-T05 | `M4_yaml.parse_comments_skipped` | unit | leading and trailing `#` ignored | `ctest -L M4` |
| M4-T05b | `M4_yaml.parse_bool_null` | unit | `true`/`false`/`null`/`~` typed | `ctest -L M4` |
| M4-T06 | `M4_yaml.parse_multiline_string` | unit | `\|`-block scalar joined with newlines | `ctest -L M4` |
| M4-T07 | `M4_loader.load_ibm_heron_r2` | integration | 156 qubits, ECR/RZ/SX/X native, heavy-hex topology resolves | `ctest -L M4` |
| M4-T08 | `M4_loader.load_ionq_forte` | integration | 36 qubits all-to-all, MS native, no mid-circuit | `ctest -L M4` |
| M4-T09 | `M4_loader.load_quantinuum_helios` | integration | 56 qubits, Full feedforward | `ctest -L M4` |
| M4-T10 | `M4_loader.load_ionq_aria_proto` | integration | the 4th chip loads (proves YAML-only-new-chip property) | `ctest -L M4` |
| M4-T11 | `M4_loader.reject_unknown_native_gate` | regression | `native_gates: [not_a_real_gate]` rejected | `ctest -L M4` |
| M4-T13 | `M4_loader.reject_id_mismatch` | regression | chip's `id` ≠ filename rejected | `ctest -L M4` |
| M4-T14 | `M4_loader.reject_kak_count_wrong` | regression | `entangler_count_max != 3` rejected | `ctest -L M4` |
| M4-T15 | `M4_loader.target_info_derives_correctly` | integration | `targetInfo` agrees with each `ChipInfo` | `ctest -L M4` |
| M4-T16 | `M4_loader.four_chips_load_same_path` | integration | exactly 4 chips load through one code path | `ctest -L M4` |
| M5-T01 | `M5_coupling.linear_distances` | unit | linear-4 distances: d(0,3)=3, d(1,2)=1 | `ctest -L M5` |
| M5-T02 | `M5_coupling.disconnected` | unit | distance INT_MAX between components | `ctest -L M5` |
| M5-T03 | `M5_coupling.all_to_all` | unit | all-to-all distances: 0/1 only | `ctest -L M5` |
| M5-T04 | `M5_placement.bell_on_linear` | unit | Bell program: q[0],q[1] placed on adjacent physical pair | `ctest -L M5` |
| M5-T05 | `M5_placement.ghz_on_linear` | unit | GHZ-3: both interaction edges on adjacent physicals | `ctest -L M5` |
| M5-T06 | `M5_routing.bell_no_swap` | integration | Bell needs zero swaps after good placement | `ctest -L M5` |
| M5-T07 | `M5_routing.long_range_inserts_swaps` | integration | adversarial layout for cx q[0],q[3]: swaps inserted, every 2q on connected pair | `ctest -L M5` |
| M5-T08 | `M5_routing.all_to_all_zero_swaps` | integration | random 4q circuit on all-to-all chip routes with zero swaps | `ctest -L M5` |
| M5-T10 | `M5_routing.target_attribute_set` | unit | output module's target attribute equals chip id | `ctest -L M5` |
| M5-T11 | `M5_routing.fourth_chip_path` | integration | the 4th chip routes through the same code path | `ctest -L M5` |
| M5-T09 | `M5_routing.invariant_holds_random_8q` | property | 50 random 8q circuits route legally on linear-8 | `ctest -L M5` |
| M6-T01 | `M6_zyz.named_gates_decompose_to_native` | unit | each named 1q gate decomposes to chip native set on IBM | `ctest -L M6` |
| M6-T02 | `M6_recipes.cx_ibm_structure` | unit | IBM CX recipe emits exactly 1 ECR + native 1q wrappers | `ctest -L M6` |
| M6-T03 | `M6_recipes.cx_ionq_matrix` | unit | IonQ CX recipe is unitary | `ctest -L M6` |
| M6-T04 | `M6_decompose.bell_ibm_no_standard` | integration | Bell after route+decompose on IBM has only native ops | `ctest -L M6` |
| M6-T05 | `M6_decompose.bell_ionq_no_standard` | integration | Bell on IonQ only has native ops | `ctest -L M6` |
| M6-T06 | `M6_decompose.bell_quantinuum_no_standard` | integration | Bell on Quantinuum only has native ops | `ctest -L M6` |
| M6-T07 | `M6_decompose.fourth_chip_path` | integration | the 4th chip decomposes through the same path | `ctest -L M6` |
| M6-T08 | `M6_cleanup.merges_rz_rz` | unit | rz(0.3) rz(0.4) merges to rz(0.7) | `ctest -L M6` |
| M6-T09 | `M6_cleanup.annihilates_inverse_pair` | unit | sx + sxdg annihilate to identity | `ctest -L M6` |
| M6-T10 | `M6_cleanup.idempotent` | property | running cleanup twice gives the same module | `ctest -L M6` |
| M7-T01 | `M7_parse.bell` | unit | bell.spn parses; expected op counts | `ctest -L M7` |
| M7-T02 | `M7_parse.ghz` | unit | ghz.spn parses; expected op counts | `ctest -L M7` |
| M7-T03 | `M7_parse.rotations` | unit | pi/2, -pi, 0.5*pi all evaluate to expected angles | `ctest -L M7` |
| M7-T04 | `M7_parse.native_ibm` | unit | native_ibm.spn under target ibm_heron_r2 parses | `ctest -L M7` |
| M7-T05 | `M7_parse.native_ionq` | unit | native_ionq.spn under target ionq_forte parses | `ctest -L M7` |
| M7-T06 | `M7_parse.mid_circuit` | unit | mid_circuit.spn parses (measure + reset present) | `ctest -L M7` |
| M7-T07 | `M7_parse.malformed_no_target` | regression | no-target program rejected with line/column | `ctest -L M7` |
| M7-T08 | `M7_parse.malformed_bad_gate` | regression | unknown gate name rejected | `ctest -L M7` |
| M7-T09 | `M7_parse.malformed_brackets` | regression | unbalanced bracket rejected with `]` mention | `ctest -L M7` |
| M7-T10 | `M7_parity.bell_structural` | golden | bell parser output matches `goldens/bell.spnir` byte-for-byte | `ctest -L M7` |
| M7-T11 | `M7_parity.ghz_structural` | golden | ghz output matches `goldens/ghz.spnir` | `ctest -L M7` |
| M7-T12 | `M7_parity.rotations_structural` | golden | rotations output matches `goldens/rotations.spnir` | `ctest -L M7` |
| M11-T01 | `M11_qasm.bell_imports` | unit | `bell.qasm` imports; expected op counts | `ctest -L M11` |
| M11-T02 | `M11_qasm.ghz_imports` | unit | `ghz.qasm` imports; 3 measures, 2 cx | `ctest -L M11` |
| M11-T03 | `M11_qasm.rotations_imports` | unit | rotations.qasm angles `pi/2`, `-pi`, `0.5*pi` | `ctest -L M11` |
| M11-T04 | `M11_qasm.mid_circuit_imports` | unit | mid-circuit measure+reset imports | `ctest -L M11` |
| M11-T05 | `M11_qasm.rejects_gate_definitions` | regression | `gate ...` rejected with Phonon hint | `ctest -L M11` |
| M11-T06 | `M11_qasm.rejects_control_flow` | regression | `if (...) { ... }` rejected | `ctest -L M11` |
| M11-T07 | `M11_qasm.parity_with_spn_bell` | integration | imported QASM bell has same op counts as SPN bell | `ctest -L M11` |
| M8-T01 | `M8_sim.bell_state_has_two_peaks` | unit | sim of bell yields |00>+|11> at 50/50 | `ctest -L M8` |
| M8-T02 | `M8_sim.ghz_state_has_two_peaks` | unit | sim of GHZ yields |000>+|111> at 50/50 | `ctest -L M8` |
| M8-T03 | `M8_sim.rotations_unitary_norm` | property | Σ|a_i|² = 1 after rotations | `ctest -L M8` |
| M8-T04 | `M8_equiv.route_preserves_meaning_bell` | integration | routed Bell ≡ portable Bell up to global phase | `ctest -L M8` |
| M8-T05 | `M8_equiv.deliberate_break_caught` | regression | H+Z vs H+X flagged inequivalent | `ctest -L M8` |
| M8-T06 | `M8_resource.bell_counts` | unit | gates=2, depth=2, qubits=2, measurements=2 | `ctest -L M8` |
| M8-T07 | `M8_resource.with_chip_fills_cost_and_error` | unit | pricePerShot × shots + error model populated | `ctest -L M8` |
| M8-T08 | `M8_resource.full_corpus_loads` | integration | every corpus file estimates cleanly | `ctest -L M8` |
| M9-T01 | `M9_qasm3.bell_emits_header` | unit | `OPENQASM`, `qubit[`, `h q[`, `cx q[`, `measure q[` present | `ctest -L M9` |
| M9-T02 | `M9_qasm3.braket_verbatim_box_present` | unit | `#pragma braket verbatim`, `box {`, `$0`, `ecr ` present | `ctest -L M9` |
| M9-T03 | `M9_qasm3.round_trip_with_qasm_importer` | integration | emitted QASM re-imports via M11 with same op count | `ctest -L M9` |
| M9-T04 | `M9_qir.bell_emits_base_profile` | unit | QIR text contains expected QIR Alliance function names + `base_profile` | `ctest -L M9` |
| M9-T05 | `M9_qir.quantinuum_emits_adaptive_profile` | unit | feedforward=Full → `adaptive_profile` | `ctest -L M9` |
| M9-T06 | `M9_qir.qubit_count_metadata_present` | unit | `required_num_qubits` / `required_num_results` set | `ctest -L M9` |
| M9-T07 | `M9_quil.bell_emits_quil` | unit | Quil emits `H 0`, `CNOT 0 1`, `MEASURE 0 ro[0]` | `ctest -L M9` |
| M9-T08 | `M9_verbatim_invariants.no_standard_gates_in_box` | property | inside the verbatim box, no `cx ` / `h ` / etc. — only native | `ctest -L M9` |
| M10-T01 | `M10_local.bell_returns_two_peaks` | integration | LocalProvider samples 2000 shots; only 00/11 appear, ~50/50 | `ctest -L M10` |
| M10-T02 | `M10_local.ghz_returns_two_peaks` | integration | 000/111 dominate the histogram | `ctest -L M10` |
| M10-T03 | `M10_local.poll_unknown_returns_error` | unit | poll on unknown id returns "error" | `ctest -L M10` |
| M10-T04 | `M10_local.name_is_local` | unit | `provider.name() == "local"` | `ctest -L M10` |
| M10-T05 | `python.test_cassette_returns_bell_histogram[ibm]` | integration | IBM cassette returns ~50/50 Bell | `ctest -L M10 -L python` |
| M10-T06 | `python.test_cassette_returns_bell_histogram[aws]` | integration | AWS cassette returns Bell | `ctest -L M10 -L python` |
| M10-T07 | `python.test_cassette_returns_bell_histogram[azure]` | integration | Azure cassette returns Bell | `ctest -L M10 -L python` |
| M10-T08 | `python.test_unknown_provider_raises` | unit | `submit(provider="not_real")` → ValueError | `ctest -L M10 -L python` |
| M10-T09 | `python.test_supported_providers_list` | unit | ibm / aws / azure / local listed | `ctest -L M10 -L python` |
| M10-T10 | `python.test_cassette_missing_raises` | regression | missing cassette → FileNotFoundError | `ctest -L M10 -L python` |
| M10-T11 | `python.test_live_ibm_smoke` | live | (skipped by default) end-to-end IBM submission with token | `SPINOR_LIVE_IBM_TEST=1` |

## How to add a row

1. After writing the test, before merging, append a row above.
2. Pick the next free `Mxx-Tnn` ID.
3. Keep "What it asserts" to one sentence.

## How to remove a row

Tests are append-only with respect to this matrix. If a test
becomes obsolete, mark it `~~Mxx-Tnn~~` (strikethrough) and
add a "removed in M??" note in the next column. Do not delete
the row — it documents history.
