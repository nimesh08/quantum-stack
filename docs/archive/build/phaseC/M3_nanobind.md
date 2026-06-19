# Phase C — M3: nanobind C++↔Python binding

## 1. Goal & spec section

Expose the engine (Phonon parser → typecheck → optimize → lower
→ Phase A pipeline → emitters / submission) to Python via a
**thin nanobind binding** named `photon._engine`. This lets the
M4 `@photon.kernel` decorator (and M5's C++ frontend's tooling
helpers) call into the C++ engine without re-implementing
compilation.

Spec section: Photon & Frontends Deep-Dive **Part 2 §5.3** (the
nanobind seam between the Python frontend and the C++ engine).

## 2. Inputs / outputs

**Input.** Python calls into `photon._engine`:

```python
import photon._engine as eng
prog = eng.compile_phonon(text="<phn-source>", target="generic")
prog.dump_phonon()       # → str (round-tripped Phonon text)
prog.dump_spinor()       # → str (lowered Spinor text)
prog.estimate()          # → ResourceEstimate
prog.histogram(shots=1000)   # simulator path; returns dict
prog.submit(provider="...", token="...")  # → JobHandle (cassette in CI)
```

**Output.** `CompiledProgram` is a thin wrapper over a
`phonon::dialect::Module` (post-typecheck/optimize) and an
optional `spinor::dialect::Module` (post-lower). Methods are
read-only after construction.

## 3. Deliverables

- `photon/bindings/include/photon/bindings/Engine.h` — C++ public
  surface `CompiledProgram` (no Python types here).
- `photon/bindings/lib/Engine.cpp` — engine wrapper that wires
  parse → typecheck → optimize → lower.
- `photon/bindings/python/Module.cpp` — nanobind module
  `photon._engine`.
- `photon/bindings/CMakeLists.txt` — gates on
  `find_package(nanobind)`; otherwise no-ops.
- `photon/bindings/python/photon/__init__.py` — Python facade
  re-exporting the binding plus a pure-Python fallback for the
  `@photon.kernel` decorator path (M4).
- `photon/tests/m3/{cpp_engine_test.cpp, py_engine_test.py}`.

## 4. Data structures

```cpp
namespace photon::bindings {

struct ResourceEstimate {
  std::size_t num_qubits;
  std::size_t depth;
  std::size_t two_qubit_count;
  std::size_t t_count;
};

class CompiledProgram {
 public:
  CompiledProgram() = default;
  static CompiledProgram fromPhononText(std::string_view phn,
                                        std::string_view target);
  bool ok() const { return ok_; }
  std::string error() const { return error_; }
  std::string dumpPhonon() const;
  std::string dumpSpinor() const;
  ResourceEstimate estimate() const;
 private:
  bool ok_ = false;
  std::string error_;
  phonon::dialect::Module phn_;
  std::optional<spinor::dialect::Module> spn_;
};

}  // namespace photon::bindings
```

The Python module exposes:

```python
class CompiledProgram:
    @property
    def ok(self) -> bool: ...
    @property
    def error(self) -> str: ...
    def dump_phonon(self) -> str: ...
    def dump_spinor(self) -> str: ...
    def estimate(self) -> ResourceEstimate: ...

def compile_phonon(text: str, target: str = "generic") -> CompiledProgram: ...
```

## 5. Algorithms

`compile_phonon` runs:

1. `phonon::parser::parse(text, "<inline>")` — parse to Phonon IR.
2. `phonon::types::typecheck(*module, opt, diag)` — linear types.
3. `phonon::optimizer::runPipeline(*module, cfg{})` — built passes.
4. `phonon::lower::lower(*module, /*target=*/nullptr)` — flat
   Spinor.
5. Returns a `CompiledProgram` carrying both modules.

`estimate()` walks the lowered Spinor module and counts:
- `alloc_qubit` ops → `num_qubits`,
- 2-qubit gate ops (cx, cz, swap, ecr, ms, rzz) →
  `two_qubit_count`,
- `t` and `tdg` ops → `t_count`,
- worst-case dependency depth using the Phonon ASAP scheduler
  if available, else the linearised op count.

## 6. Test matrix

| ID | Type | Inputs | Expected | Gate |
| --- | --- | --- | --- | --- |
| M3.cpp.compile_phonon | U | bell-pair Phonon text | program ok, ≥6 ops in spinor | M3 |
| M3.cpp.estimate | U | bell-pair | n_qubits=2, two_q=1 | M3 |
| M3.cpp.error_propagation | U | broken Phonon text | program ok=false, error non-empty | M3 |
| M3.py.compile_phonon | I | same | round-trips through nanobind | M3 |
| M3.py.dump_spinor | I | bell-pair | non-empty Spinor text | M3 |
| M3.py.estimate | I | bell-pair | dict-like fields populated | M3 |
| M3.py.import | U | `import photon` succeeds | no exception | M3 |

## 7. Cookbook example

```python
import photon._engine as eng
prog = eng.compile_phonon(
  '''target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c[0] = measure q[0]
c[1] = measure q[1]
''', target="generic")
print(prog.ok)        # True
e = prog.estimate()
print(e.num_qubits, e.two_qubit_count)   # 2 1
```

## 8. Pitfalls

- **GIL.** The binding holds the GIL by default; long-running
  compilations should release it. M3 doesn't bother (fast).
- **String ownership.** Return Python strings as `nb::str` made
  from `std::string`, not `string_view` — caught by
  `M3.py.dump_spinor` (would crash on access).
- **No nanobind on the build host.** CMake guards on
  `find_package(nanobind)`; failure logs a warning and skips.
  The pure-Python fallback in `photon/__init__.py` runs the
  Phonon CLI by subprocess for the M4 decorator path.

## 9. Definition of Done

- [ ] `photon._engine` import works when nanobind is present.
- [ ] `cpp_engine_test` passes (engine wrapper alone, no Python).
- [ ] `py_engine_test.py` passes when nanobind is present;
      skipped (with a clear note) otherwise.
- [ ] `phaseC/test-matrix.md` updated.
- [ ] `phaseC_progress.md` appended.

## 10. Open questions

None blocking.
