# Phase C — M6: Convergence check

## 1. Goal & spec section

Prove that all three frontends — Photon `.pho` source, Python
`@photon.kernel`, and C++ `[[photon::kernel]]` — produce
**equivalent** compiled output for the same logical program.

This is the architectural payoff of Phase C: three doors, one
engine.

Spec section: Photon & Frontends Deep-Dive **Part 2 §7**
(`Diagram 4 — Two frontends, one engine: Python, C++, and Photon
are three doors into the same Phonon IR`).

## 2. Inputs / outputs

**Inputs.** For each program in the convergence corpus
(`bell`, `ghz3`), three source forms:

- `bell.pho` — Photon source.
- `bell.py` — Python decorated kernel.
- `bell.cpp` — marked C++ kernel.

**Output.** For each program, three compiled Spinor modules
that satisfy:

1. **Op-count equivalence.** Each module has the same gate-count
   profile (#H, #CX, #measure, etc.).
2. **Resource-estimate equivalence.** `num_qubits`, `two_qubit_count`,
   and `t_count` match exactly.
3. **Structural equivalence.** The sequence of op kinds (modulo
   the alloc ops, whose order is bookkeeping-driven) is the same.

A stronger property — full **matrix equivalence** on a simulator
— is also tested using Phase A's existing equivalence harness
(via the engine), but is gated on the simulator being available.

## 3. Deliverables

- `photon/tests/m6/convergence_test.cpp` — drives all three
  paths and asserts the three properties above.
- 3 source files per program under
  `photon/tests/corpus/convergence/<name>.{pho,py,cpp}`.
- `photon/tests/m6/CMakeLists.txt`.

## 4. Data structures

```cpp
struct GateProfile {
  int num_qubits = 0;
  int h = 0, x = 0, y = 0, z = 0;
  int s = 0, t = 0;
  int rx = 0, ry = 0, rz = 0;
  int cx = 0, cz = 0, swap = 0;
  int measure = 0;

  bool operator==(const GateProfile&) const = default;
};

GateProfile profileSpinor(const spinor::dialect::Module& m);
```

## 5. Algorithms

For each `<name>` in the corpus:

1. Parse / ingest each form into a `photon::lang::Module`.
2. Lower to Phonon via `photon::lang::lowerToPhonon`.
3. Run through `CompiledProgram::fromPhononModule` to produce a
   compiled Spinor module.
4. Profile the Spinor module and assert all three profiles
   compare equal.

For Python: at link-time the Python decorator is invoked from C++
through a small subprocess that emits the produced Phonon text.
The convergence test treats the Python output as another form of
Phonon source; it doesn't depend on the C++ engine bindings the
Python decorator uses internally.

## 6. Test matrix

| ID | Type | Inputs | Expected | Gate |
| --- | --- | --- | --- | --- |
| M6.bell.pho_vs_cpp | E | bell.pho, bell.cpp | identical Spinor profile | M6 |
| M6.bell.pho_vs_python | E | bell.pho, bell.py | identical Spinor profile | M6 |
| M6.ghz3.three_way | E | ghz3.{pho,py,cpp} | identical Spinor profile | M6 |
| M6.bell.same_estimate | E | three forms | identical ResourceEstimate | M6 |

## 7. Cookbook example

```cpp
auto ph = compileFromPho("bell.pho");
auto cp = compileFromCpp("bell.cpp", "bell_kernel");
auto py = compileFromPyText(<phonon-text-emitted-by-decorator>);

auto pPh = profileSpinor(*ph.spinor());
auto pCp = profileSpinor(*cp.spinor());
auto pPy = profileSpinor(*py.spinor());

EXPECT_TRUE(pPh == pCp);
EXPECT_TRUE(pPh == pPy);
```

## 8. Pitfalls

- **Op order drift.** Phonon's optimizer is deterministic but it
  reorders some ops; we compare profiles (counts), not raw
  sequences, to keep the test robust.
- **Python sample available without nanobind.** When `_engine` is
  absent, the convergence test gets the Python-emitted Phonon
  text from a static fixture file `bell.py.phn` (recorded
  cassette). On developer machines with the extension built, the
  test re-records before comparing.
- **Library expansion identity.** `q.ghz()` on a 3-qubit register
  is identical across all three doors because `photon.lib`
  expanders are deterministic (M2). Caught by `M6.ghz3.three_way`.
- **Targets propagate.** Each form sets the same `target`, and the
  engine carries it; convergence is asserted both for `target
  generic` and (smoke-tested) for a real chip id.

## 9. Definition of Done

- [ ] All M6 tests pass.
- [ ] The convergence corpus has at least Bell + GHZ3 in three
      forms each.
- [ ] Three-way profile equivalence asserted.
- [ ] Three-way ResourceEstimate equivalence asserted.
- [ ] `phaseC/test-matrix.md` updated.
- [ ] `phaseC_progress.md` appended.
- [ ] **End-of-phase docs:** `phaseC_photon_guide.md` filled in
      end-to-end with three on-ramps and a worked example per
      frontend.
- [ ] **Stop.** Phase C done; Phase D opens in a fresh chat.

## 10. Open questions

None. End of Phase C.
