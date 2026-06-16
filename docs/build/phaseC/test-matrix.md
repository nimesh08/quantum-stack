# Phase C — test matrix

Single-page index. Updated as each milestone lands. Types:
**U** unit, **I** integration, **P** property, **G** golden,
**R** regression, **F** fuzz, **E** end-to-end.

> Filled in as each milestone's spec is finalised. The columns are
> stable; rows append in milestone order.

| ID | Type | Inputs | Expected | CI gate |
| --- | --- | --- | --- | --- |
| M1.builder.ast_helpers | U | direct AST construction | mkBinOp/mkInt/etc round-trip | M1 |
| M1.builder.types | U | type ctors | sizes preserved | M1 |
| M1.builder.module_lookup | U | Module + Function | findFunction works | M1 |
| M1.parser.bell | U | bell.pho | kernel `bell` parsed, body >= 4 stmts | M1 |
| M1.parser.ghz | U | ghz.pho | kernel `ghz` parsed | M1 |
| M1.parser.target_carries | U | bell_kernel.pho | target = ibm_heron_r2 | M1 |
| M1.parser.for_loop | U | for_loop.pho | parses | M1 |
| M1.parser.if_else | U | if_else.pho | parses | M1 |
| M1.parser.error_unknown_top_level | U | garbage source | diagnostic emitted | M1 |
| M1.parser.error_missing_paren | U | malformed source | parse fails | M1 |
| M1.parser.comments_all_kinds | U | `#`, `;`, `//`, `/* */` | parses | M1 |
| M1.lower.bell | I | bell.pho | exact op counts | M1 |
| M1.lower.ghz | I | ghz.pho | exact op counts | M1 |
| M1.lower.for_loop | I | for_loop.pho | phonon.for emitted | M1 |
| M1.lower.if_else | I | if_else.pho | phonon.if emitted | M1 |
| M1.lower.target_propagated | I | bell_kernel.pho | target survives | M1 |
| M1.lower.def_wrapper | I | bell.pho | phonon.def + end_def emitted | M1 |
| M1.e2e.bell_verifies | E | bell.pho | Phonon verifier accepts | M1 |
| M1.e2e.ghz_verifies | E | ghz.pho | Phonon verifier accepts | M1 |
| M2.library_routine_set | U | known names | recognised; unknown rejected | M2 |
| M2.bell_pair | I | lib_bell.pho | 1 H + 1 CX | M2 |
| M2.ghz_n3 | I | lib_ghz.pho | 1 H + 2 CX + 3 measure | M2 |
| M2.qft_n3 | I | lib_qft.pho | 3 H + 6 CX + 1 swap | M2 |
| M2.grover_rounds2_emits_oracle_calls | I | lib_grover.pho | 2 phonon.call + diffusion blocks | M2 |
| M2.teleport_emits_corrections | I | lib_teleport.pho | 2 measure + 2 phonon.if | M2 |
| M2.vqe_depth2 | I | lib_vqe.pho | 6 ry + 4 cx | M2 |
| M2.e2e_verifies | E | lib_bell.pho | Phonon verifier accepts | M2 |
| M3.cpp.compile_phonon_bell | U | inline Phonon source | program ok, dumps non-empty | M3 |
| M3.cpp.estimate_bell | U | bell-pair | n_qubits=2, two_q=1, depth>=4 | M3 |
| M3.cpp.error_propagation | U | broken Phonon text | ok=false, error non-empty | M3 |
| M3.py.import | I | `import photon` | exposes compile_phonon, kernel | M3 |
| M3.py.compile_phonon | I | bell-pair via nanobind | ok=true, spinor dump non-empty | M3 |
| M3.py.estimate | I | bell-pair via nanobind | num_qubits=2, two_qubit_count=1 | M3 |
| M4.translator.bell | U | `def bell()` body | Phonon text contains `qubit q[2]`, `h q[0]`, `cx q[0], q[1]` | M4 |
| M4.translator.ghz_via_lib | U | `q.ghz()` | full chain emitted | M4 |
| M4.translator.for_loop | U | `for i in range(0,4):` | `for i in 0..4 {` emitted | M4 |
| M4.translator.target_propagated | U | decorator `target=…` | first line `target …` | M4 |
| M4.rejection.while | U | `while True:` | `UnsupportedConstructError` mentions `while` | M4 |
| M4.rejection.import | U | `import os` in body | error mentions `import` | M4 |
| M4.rejection.unknown_method | U | `q.foo(0)` | error names `foo` | M4 |
| M4.rejection.try | U | `try:` block | error mentions `try` | M4 |
| M4.decorator.bell_compiles | I | decorated bell | compiled.ok=true, n_qubits=2 | M4 |
| M4.decorator.ghz_compiles | I | decorated ghz3 | compiled.ok=true, two_qubit_count=2 | M4 |
| M4.decorator.run_histogram | I | `bell.run(shots=100)` | dict whose values sum to 100 | M4 |
| M4.decorator.target_kwarg | I | `@photon.kernel(target=…)` | `target` attribute correct | M4 |
| M4.decorator.lib_ghz_method | I | `q.ghz()` in decorated body | engine compiles | M4 |
| M5.ingest.bell | U | bell.cpp | photon::lang::Module produced; lowers to 2 H+1 CX+2 alloc_qubit | M5 |
| M5.ingest.ghz | U | ghz.cpp | 1 H + 2 CX | M5 |
| M5.ingest.lib_grover | U | lib_grover.cpp | 2 phonon.call (oracle hooks) | M5 |
| M5.ingest.no_kernel_marker | U | unmarked function with matching name | promoted to kernel | M5 |
| M5.ingest.unknown_construct | U | `asm(...)` in body | error diagnostic | M5 |
| M5.photonc_cli.bell_compiles | E | YAML + bell.cpp via `photonc-cxx` | stdout: `num_qubits=2 two_qubit_count=1` | M5 |
| M6.convergence.bell_pho_vs_cpp | E | bell.{pho,cpp} | identical Spinor profile | M6 |
| M6.convergence.ghz3_pho_vs_cpp | E | ghz3.{pho,cpp} | identical Spinor profile | M6 |
| M6.convergence.bell_resource_estimate | E | three forms | identical ResourceEstimate | M6 |
| M6.python.three_door_bell | E | decorated bell + corpus | gate-count match, engine ok | M6 |
| M6.python.three_door_ghz3 | E | decorated ghz3 | num_qubits=3, two_qubit_count=2 | M6 |
| (M5) | | | | M5 |
| (M6) | | | | M6 |
