# Phase C (Photon and the frontends) — progress journal

Append-only build journal for Phase C. New entries go on top.

For per-milestone engineering specs see [`phaseC/`](phaseC/README.md).
For deviations from the Photon Deep-Dive see
[`phaseC_decisions.md`](phaseC_decisions.md).

---

## 2026-06-16 — M6: convergence check + Phase C complete

What landed:

- 6-file convergence corpus: bell.{pho,py,cpp}, ghz3.{pho,py,cpp}.
- `photon/tests/m6/convergence_test.cpp` — three-way Spinor
  profile equivalence + ResourceEstimate equivalence.
- `photon/tests/m6/three_door_test.py` — Python-side smoke
  test that the decorator emits the expected statement set.
- `phaseC_photon_guide.md` finalised: three on-ramps, worked
  examples for every front door (Photon `.pho`, Python decorator,
  C++ ingester), pinned versions table, glossary pointer.
- `phase-c-ci.yml` workflow added.

Tests: **45/45 passing** (43 prior + 2 new M6 entries).

End of Phase C. Phase D opens in a fresh chat.

---

## 2026-06-16 — M5: C++ Clang LibTooling frontend landed

What landed:

- `photon/frontends/cpp/include/photon/cppfront/Ingest.h` —
  public `ingestCpp(source, entry, target) -> photon::lang::Module`.
- `photon/frontends/cpp/lib/Ingest.cpp` — self-hosted
  recursive-descent ingester for the small C++ subset Photon
  kernels use (`[[photon::kernel]]`, `QReg q(N)`, gate methods,
  `for`, `if`, `return q.measure_int()`). LibTooling path wired
  behind `PHOTON_HAVE_CLANG` (not exercised in CI; LLVM/Clang
  not on the build host).
- `photon/frontends/cpp/cli/Photonc.cpp` — `photonc-cxx` CLI
  driver that reads a YAML build config and prints a resource
  estimate.
- `photon/tests/m5/{ingest_test.cpp, photonc_cli_test.cpp}` —
  five ingest assertions + a CLI smoke test.
- 3 corpus C++ kernels under `photon/tests/corpus/cpp/`.
- Engine wrapper got a `fromPhononModule(...)` constructor so
  the C++ frontend (and M6) can hand a built IR to the engine
  without re-parsing printed text.
- The Photon `lowerToPhonon` now emits flat top-level Phonon
  programs for parameterless kernels (no `phonon.def` wrapper),
  matching the shape Phonon's `lower::lower` expects.
- Verified live: `photonc-cxx build.yaml` prints
  `compiled. target=generic shots=1024 num_qubits=2 two_qubit_count=1 depth=4`.

Tests: **43/43 passing** (41 prior + 2 new M5 entries).

What's next: M6 — convergence check. Spec lands at
`docs/build/phaseC/M6_convergence.md` before any code in
`photon/tests/m6/`.

---

## 2026-06-16 — M4: Python `@photon.kernel` frontend landed

What landed:

- `photon/frontends/python/photon/_translator.py` — `ast`-based
  AST visitor mapping Python kernel bodies to Phonon source text.
- `photon/frontends/python/photon/_decorator.py` — real
  decorator that translates, compiles via the engine, and exposes
  `phonon_text`, `compiled`, `target`, `run(shots, target=…)`.
- `photon/frontends/python/photon/_errors.py` — typed errors:
  `UnsupportedConstructError`, `CompilationError`.
- `photon/tests/m4/{translator_test.py, decorator_test.py}` —
  12 assertions across translator, rejection, and decorator.
- Photon python-package staging is now an `add_custom_target`
  (rebuilds incrementally).
- Verified live: `@photon.kernel def bell(): … return q.measure_int()`
  compiles, returns the right `ResourceEstimate`, and runs through
  `bell.run(shots=100)` producing the expected histogram shape.

Tests: **41/41 passing** (39 prior + 2 new M4 entries, 12
internal assertions).

What's next: M5 — C++ Clang LibTooling frontend. Spec lands at
`docs/build/phaseC/M5_cpp_frontend.md` before any code in
`photon/frontends/cpp/`.

---

## 2026-06-16 — M3: nanobind binding landed

What landed:

- `photon/bindings/include/photon/bindings/Engine.h` —
  `CompiledProgram` (parse → typecheck → optimize → lower).
- `photon/bindings/lib/Engine.cpp` — engine wrapper.
- `photon/bindings/python/Module.cpp` — nanobind module
  `photon._engine` exposing `compile_phonon`, `CompiledProgram`,
  `ResourceEstimate`.
- `photon/bindings/python/photon/{__init__,_decorator,_qreg}.py`
  — Python facade: `import photon` works; M4 fills in the
  decorator's AST walk.
- CMake gates the extension on `find_package(nanobind)` (with a
  fallback that probes the pip-installed `nanobind.cmake_dir()`).
- `photon/tests/m3/{cpp_engine_test.cpp, py_engine_test.py}` —
  pure-C++ engine tests + Python round-trip tests.
- Top-level `CMAKE_POSITION_INDEPENDENT_CODE=ON` so the static
  engine libs link cleanly into the shared module.
- Verified live on the build host: `import photon;
  photon.compile_phonon(...).estimate()` returns the expected
  shape (Bell: 2 qubits, 1 two-qubit gate).

Tests: **39/39 passing** (37 prior + 2 new M3 entries).

What's next: M4 — `@photon.kernel` Python frontend. Spec lands
at `docs/build/phaseC/M4_python_frontend.md` before any code in
`photon/frontends/python/`.

---

## 2026-06-16 — M2: photon.lib landed

What landed:

- `photon/lang/lib/Library.{h,cpp}` — seven library expanders:
  `bell_pair`, `ghz`, `qft`, `iqft`, `grover`, `teleport`,
  `vqe_ansatz`. Wired into the lowerer's `LibCall` case.
- `photon/tests/m2/lib_test.cpp` — 8 assertions covering every
  routine + an e2e verifier check.
- 6 corpus driver kernels under `photon/tests/corpus/lib_*.pho`.

Tests: **37/37 passing** (36 prior + 1 new M2 entry, 8 internal
assertions).

What's next: M3 — nanobind C++↔Python binding. Spec lands at
`docs/build/phaseC/M3_nanobind.md` before any code in
`photon/bindings/`.

---

## 2026-06-16 — M1: Photon language core landed

What landed:

- `photon/lang/` — `Lexer`, `Parser`, `Module` (AST), `Lower`
  (Photon → Phonon).
- `photon/tests/m1/` — 4 test executables (`builder_test`,
  `parser_test`, `lower_test`, `e2e_test`), 19 assertions total.
- `photon/tests/corpus/` — `bell.pho`, `ghz.pho`,
  `bell_kernel.pho`, `for_loop.pho`, `if_else.pho`,
  `search_kernel.pho`.
- `photon/CMakeLists.txt` reactivated with `add_subdirectory(lang)`
  and `add_subdirectory(tests/m1)`; works without LLVM/MLIR
  (in-tree path).

Tests: **36/36 passing** (32 prior + 4 new M1 entries).

What's next: M2 — `photon.lib`. Spec lands at
`docs/build/phaseC/M2_lib.md` before any code in `photon/lang/lib/`.

---

## 2026-06-16 — M0: Phase C scaffolding landed

What landed:

- `docs/build/phaseC/` populated: `README.md`, `M0_overview.md`,
  `spec_template.md`, `glossary.md`, `test-matrix.md`,
  `M1_lang.md`.
- This file (`phaseC_progress.md`), `phaseC_decisions.md`, and
  the (empty) `phaseC_photon_guide.md` created.
- `versions.md` updated with Phase C pins (LLVM/Clang/MLIR
  **22.1.8**, nanobind **2.12.0**, CPython **3.13** target /
  3.12 floor).

What's next: M1 — Photon language core. Spec lands at
`docs/build/phaseC/M1_lang.md` before any code in `photon/lang/`.
