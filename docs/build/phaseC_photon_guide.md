# Phase C — Photon User Guide

End-of-phase user-facing guide. The document a person new to the
project reads to understand and use Photon and the frontends.

For per-milestone engineering specs see
[`phaseC/`](phaseC/README.md). For the build journal see
[`phaseC_progress.md`](phaseC_progress.md). For deviations from
the Photon Deep-Dive see [`phaseC_decisions.md`](phaseC_decisions.md).

---

## Three on-ramps

Pick yours:

- **From quantum.** Skip §1. Read §2 (the Photon language and
  `photon.lib`) and §6 (write & run a program).
- **From compilers.** Skip §2. Read §1 (a one-page primer linking
  to Phonon and Spinor) and §3 (the three-door, one-engine
  convergence story).
- **From neither.** Read §1 → §2 → §3 → §4 → §5 → §6 in order.

The full glossary lives at
[`phaseC/glossary.md`](phaseC/glossary.md).

---

## §1 — Where Photon sits

Photon is the **top** of a four-layer stack:

```
Photon         <- you write here (Phase C)
  v
Phonon         <- chip-independent IR + the optimizer (Phase B)
  v
Spinor         <- chip-specific portable assembly (Phase A)
  v
provider       <- IBM / AWS / Azure / direct vendor APIs
```

A Photon program is OO source: quantum registers as objects,
gates as methods, library calls for the famous algorithms. It
lowers to Phonon (which the typechecker, optimizer, and lowerer
already process), then to Spinor (which places, routes,
decomposes, and emits).

---

## §2 — The Photon language and `photon.lib`

A Photon program (`.pho` file) declares one or more functions, at
least one marked `kernel`. Kernels are the entry points the
runtime can execute.

```
target generic

kernel bell() -> int {
  QReg q(2)        // a 2-qubit register, as an object
  q.h(0)           // Hadamard on slot 0
  q.cx(0, 1)       // CNOT, control 0, target 1
  return q.measure_int()
}
```

What's available:

- **Types.** `int`, `angle` (radians), `bit`, `QReg(N)`, plus
  user-defined `Oracle` callbacks for `lib.grover`.
- **Gate methods on `QReg`.** Every Spinor gate the engine
  supports: `h`, `x`, `y`, `z`, `s/sdg`, `t/tdg`, `rx/ry/rz`,
  `cx`, `cz`, `swap`, `sx/sxdg`, `ecr`, `ms`, `rzz`, `gpi`,
  `gpi2`, `u1q`, plus aliases `cnot` / `hadamard` / `phase`.
- **Control flow.** `for i in lo..hi { ... }` (compile-time
  bounds), `if (cond) { ... } else { ... }`.
- **Measurement.** `q.measure()` returns a bit register;
  `q.measure_int()` returns the joint outcome as an unsigned int.
- **Library.** `q.bell_pair(a, b)`, `q.ghz()`, `q.qft()`,
  `q.iqft()`, `q.grover(oracle, rounds)`, `q.teleport(s, a, d)`,
  `q.vqe_ansatz(depth)`. All seven implemented in
  `photon/lang/lib/Library.cpp`.

What is **not** available (deliberately):

- Free-function calls outside `photon.lib`. Add new library
  routines in `photon/lang/lib/Library.cpp` instead.
- `while` loops without a compile-time bound (use `for`).
- Recursion. Inline the body or split into a `for` loop.
- Anything dynamic (file I/O, network, allocation in a kernel).

The full grammar lives in [`phaseC/M1_lang.md`](phaseC/M1_lang.md).

---

## §3 — The three doors, one engine

The architectural payoff of Phase C: three frontends, one
compiled artifact.

```
.pho source                  Python @photon.kernel function
   │                                       │
   │ photon::lang::parse                   │ inspect.getsource → ast.parse
   │                                       │ photon._translator
   │                                       │
   ▼                                       ▼
              photon::lang::Module    (the Photon AST)
                              │
                              ▼
              photon::lang::lowerToPhonon
                              │
                              ▼
                   phonon::dialect::Module
                              │
              ┌───────────────┼───────────────┐
              │               │               │
        types::typecheck  optimizer::run  lower::lower → spinor::Module
              │               │               │
              └───────────────┴───────────────┘
                              │
                  Phase A: place → route → decompose → emit → submit
```

The C++ frontend (`photon/frontends/cpp/`) is the third door.
Its ingester reads `[[photon::kernel]]`-marked functions from
`.cpp` source and produces the same `photon::lang::Module`; from
that point the path is identical.

**Convergence is enforced** by `photon/tests/m6/convergence_test.cpp`,
which compiles the same logical program in all three forms and
asserts identical Spinor profiles + ResourceEstimates.

---

## §4 — The Python frontend (`@photon.kernel`)

Eight lines of plain Python:

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.phonon_text)
print(bell.compiled.estimate().num_qubits)   # 2
print(bell.run(shots=1000))                   # {'00': 1000}  (stub)
```

Mechanism. The decorator uses Python's standard-library
introspection — `inspect.getsource`, `ast.parse`, an
`ast.NodeVisitor` subclass — to walk the function body and emit
Phonon. Then `photon._engine` (the nanobind binding) compiles
that Phonon through the C++ engine. The Python frontend never
re-implements any compiler logic; it only translates source.

Supported constructs: `QReg(N)`, gate methods, library methods,
counted `for i in range(lo, hi)`, `if c == k`, `return
q.measure_int()`. Everything else is rejected with a precise
`UnsupportedConstructError` naming the offending Python construct
and its line/column.

---

## §5 — The C++ frontend (Clang LibTooling)

Mark a kernel in ordinary C++:

```cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
```

Drive it from a small YAML config:

```yaml
build:
  sources: [bell.cpp]
  entry: bell_kernel
  target: generic
  shots: 1024
```

```bash
$ photonc-cxx build.yaml
compiled. target=generic shots=1024 num_qubits=2 two_qubit_count=1 depth=4
```

Mechanism. The default path is a self-hosted recursive-descent
parser for the small C++ subset Photon kernels use (intentionally
not a full C++ parser; rejects anything outside the subset). A
LibTooling-backed path (`#ifdef PHOTON_HAVE_CLANG`) is wired but
gated on `find_package(Clang)`; CI runs the self-hosted path so
the build is hermetic.

---

## §6 — Installing & running

**System requirements.**

| Component | Pin | Notes |
| --- | --- | --- |
| LLVM / Clang / MLIR | 22.1.8 | engine; final 22.1.x release |
| C++ | 20 | engine + frontends |
| CPython | 3.13 (3.12 floor) | decorator + facade |
| nanobind | 2.12.0 | C++↔Python binding |
| CMake | 3.28+ | build system |

**Build.**

```bash
cd quantum-stack
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

**Run a `.pho` file** (programmatic; the user-facing
`photonc` driver lands in Phase D):

```cpp
#include "photon/bindings/Engine.h"
#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"

auto pr = photon::lang::parse(read_file("bell.pho"), "bell.pho");
auto lr = photon::lang::lowerToPhonon(*pr.module);
auto cp = photon::bindings::CompiledProgram::fromPhononModule(
    std::move(*lr.module), "generic");
auto e  = cp.estimate();
```

**Run a Python kernel.** With `pip install -e photon/frontends/python`
(or by adding `build/photon/bindings/python` to `PYTHONPATH`):

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.compiled.dump_spinor())
```

**Run a C++ kernel.**

```bash
photonc-cxx build.yaml
```

---

## §7 — Pinned versions

Re-verified upstream **2026-06-16**.

| Component | Pin | Source |
| --- | --- | --- |
| LLVM / Clang / MLIR | **22.1.8** | github.com/llvm/llvm-project |
| C++ | C++20 | — |
| Eigen | 5.0.1 | gitlab.com/libeigen/eigen |
| nanobind | **2.12.0** (BSD-3-Clause) | github.com/wjakob/nanobind |
| CPython | **3.13** target / **3.12** floor | python.org |

Decisions D13–D17 in
[`phaseC_decisions.md`](phaseC_decisions.md) explain everything
that deviates from the deep-dive.

---

## §8 — Where to put new code

| You're adding… | Folder |
| --- | --- |
| A `.pho` keyword or grammar rule | `photon/lang/cpp/lib/` |
| A `photon.lib` algorithm | `photon/lang/cpp/lib/Library.cpp` |
| A nanobind type or function | `photon/bindings/python/Module.cpp` |
| A Python AST handler | `photon/frontends/python/photon/_translator.py` |
| A Clang AST visitor or matcher | `photon/frontends/cpp/lib/Ingest.cpp` |
| A test | `photon/tests/<m1..m6>/` |
| Anything in Spinor or Phonon | **Stop.** Wrong phase. |

---

## §9 — Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `@photon.kernel cannot translate ...` | The Python construct is outside Photon's supported subset. The error names the offending node (`while`, `import`, `try`, `q.unknown_method`, etc.). |
| `parse failed` from the engine | The `.pho` source had a syntax error. Run `photon::lang::parse` directly and inspect `pr.diag` for the line/column. |
| `typecheck failed` | Linear-type violation: a qubit was used twice as input, or used after `measure` without `reset`. The Phonon checker (Phase B M3) names the value. |
| `photon._engine` import fails | The nanobind extension wasn't built. Re-run cmake after installing `nanobind` (`pip install nanobind`) and pass `-Dnanobind_DIR=$(python3 -c 'import nanobind;print(nanobind.cmake_dir())')`. |
| `photonc-cxx: no [[photon::kernel]]-marked function` | The `entry` in the YAML didn't match a marked function. Either add the `[[photon::kernel]]` attribute or change `entry`. |
| Convergence test fails | The three frontends produced different op counts. Diff the Spinor profiles in the failure output; almost always a translator bug. |

---

## §10 — Definition of Done

All boxes ticked at end-of-phase:

- [x] `bell` runs through every front door and produces the same
      compiled Spinor profile (M6 convergence test).
- [x] `@photon.kernel` rejects every non-quantum construct with
      a precise diagnostic.
- [x] No frontend reimplements compilation; all three converge on
      one Phonon IR and one C++ engine.
- [x] All Phase C milestone tests pass in CI: **13 photon ctest
      entries** (M1 × 4, M2 × 1, M3 × 2, M4 × 2, M5 × 2, M6 × 2)
      on top of Phase A's 15 + Phase B's 17 (= 45 total).
- [x] `phaseC_progress.md`, `phaseC_decisions.md`, and this guide
      are written.
- [x] **Stop.** Do **not** start the Platform (Phase D). The next
      phase is a fresh agent conversation per the handoff brief.

---

## §11 — Glossary

See [`phaseC/glossary.md`](phaseC/glossary.md). If you find a
term not listed there, it is a documentation bug — please file
or fix.
