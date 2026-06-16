# Phase C — M4: Python `@photon.kernel` frontend

## 1. Goal & spec section

The Python frontend: developers write ordinary Python, decorate
the function with `@photon.kernel`, and the decorator translates
the function body into Phonon, hands it to the engine, and
returns a callable that runs the kernel.

Spec section: Photon & Frontends Deep-Dive **Part 2 §§5.1–5.3**.

## 2. Inputs / outputs

**Input.** A Python function decorated with `@photon.kernel`.
The function takes no positional quantum arguments (those are
created inside, like `q = photon.QReg(2)`); it may accept Python
parameters that lower to Phonon classical scalars (`int`, `float`).

**Output.** The decorator returns a `_PhotonKernel` object whose
`__call__` invokes the compiled program. It also exposes:

- `kernel.phonon_text` — the Phonon source produced by the
  decorator,
- `kernel.compiled` — the `photon._engine.CompiledProgram` (or
  `None` if compilation failed),
- `kernel.run(shots=…, target=…)` — submits / simulates and
  returns a histogram dict.

**Supported constructs (M4 scope).**

| Python construct | Phonon mapping |
| --- | --- |
| `q = photon.QReg(N)` | `qubit q[N]; bit __c_q[N]` |
| `q.h(i)`, `q.cx(a, b)`, etc. | corresponding gate stmts |
| `q.measure_int()` (in return) | per-slot measure + return |
| `for i in range(lo, hi):` | `for i in lo..hi { ... }` |
| `if c == 1:` (post-measure) | `if (c[0] == 1) { ... }` |
| `int n = 5; theta = 3.14` | `int n = 5; angle theta = 3.14` |
| numeric ops on Python ints / floats | folded at parse time |

**Rejected (M4 scope, with precise messages).**

- File I/O, network, `os`, `subprocess`, `import` inside body.
- `while` (no compile-time bound).
- Arbitrary function calls except `photon.QReg(...)` and
  `q.<gate_method>(...)` and `q.<lib_routine>(...)`.
- `try` / `raise` / `with` / generators / `async`.
- Lambdas inside the kernel.

## 3. Deliverables

- `photon/frontends/python/photon/_decorator.py` — replaces the
  M3 stub. Implements the `_PhotonKernel` class and the
  `kernel()` decorator factory.
- `photon/frontends/python/photon/_translator.py` — the
  AST-walking translator emitting Phonon text.
- `photon/frontends/python/photon/_errors.py` — typed errors.
- `photon/tests/m4/{decorator_test.py, translator_test.py,
  rejection_test.py, e2e_test.py}` + `CMakeLists.txt`.

## 4. Data structures

```python
class _PhotonKernel:
    phonon_text: str
    compiled: Optional[photon._engine.CompiledProgram]
    target: str
    name: str

    def run(self, shots: int = 1024, target: Optional[str] = None) -> dict:
        ...
```

## 5. Algorithms

The translator is an `ast.NodeVisitor` subclass whose
`visit_FunctionDef` walks the body and writes Phonon text into a
buffer:

1. Strip the `@photon.kernel` decorator from the function (it's
   already been applied by the time we run; just skip the
   `decorator_list` entries).
2. The function name becomes the Phonon `def`.
3. Walk statements:
   - `Assign` to a `Name` from a `Call(photon.QReg, [N])` →
     register a QReg of size N.
   - `Expr(Call(Attribute(Name(q), method), args))` →
     gate or library call on register `q`.
   - `For(Name(i), Call(Name('range'), [lo, hi]), body, [])` →
     `for i in lo..hi { walk body }`.
   - `If(Compare(Name(c), [Eq()], [Constant(int)]), body, orelse)`
     → `if (c == 1) { ... } else { ... }`. The receiver `c` must
     refer to a previously-recorded measurement result.
   - `Return(Call(Attribute(Name(q), 'measure_int'), []))` →
     `<measure all of q's slots; pack into int; return that>`.
4. Anything else: collect a precise diagnostic referring to the
   AST node's `lineno` / `col_offset`.

After walking, the translator produces the Phonon text (a string)
and hands it to `photon._engine.compile_phonon(text, target)`.

## 6. Test matrix

| ID | Type | Inputs | Expected | Gate |
| --- | --- | --- | --- | --- |
| M4.translator.bell | U | `def bell(): ...` | Phonon text matches golden | M4 |
| M4.translator.ghz | U | `def ghz(): ...` | Phonon text matches golden | M4 |
| M4.translator.for_loop | U | `for i in range(0, N): q.h(0)` | `for i in 0..N { h q[0] }` | M4 |
| M4.translator.measure_int_return | U | `return q.measure_int()` | per-slot measure + return | M4 |
| M4.decorator.calls_engine | I | decorated bell | `kernel.compiled.ok == True` | M4 |
| M4.rejection.file_io | U | `open('x')` in body | clear diagnostic | M4 |
| M4.rejection.while | U | `while True:` | clear diagnostic | M4 |
| M4.rejection.unknown_method | U | `q.foo(0)` | clear diagnostic | M4 |
| M4.rejection.import | U | `import os` in body | clear diagnostic | M4 |
| M4.e2e.bell_runs | E | the deep-dive's 8-line example | `kernel.compiled.ok` AND Spinor dump non-empty | M4 |

## 7. Cookbook example

The deep-dive's 8-line Bell:

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.phonon_text)
print(bell.compiled.estimate().num_qubits)  # 2
```

## 8. Pitfalls

- **`inspect.getsource` and indented sources.** When the decorator
  is applied to a method on a class, `getsource` returns the body
  with leading whitespace; `textwrap.dedent` is required.
- **`ast.parse` line numbers.** `lineno` is relative to the source
  string, not the file; for diagnostics we offset by
  `inspect.getsourcelines(...)[1]`.
- **Closures.** Free variables (kernel referring to module-level
  ints) are resolved at translation time by looking up
  `func.__globals__` and `func.__closure__`.
- **`measure_int` outside `return`.** `q.measure_int()` is only
  valid as the value of a `Return`. A bare statement is rejected
  with a clear message (M4.rejection covers this implicitly).
- **Decorator ordering.** Multiple decorators stack outside-in;
  `@photon.kernel` must be the innermost so it sees the original
  function. Diagnostic emitted otherwise.

## 9. Definition of Done

- [ ] All M4 tests pass.
- [ ] The 8-line Bell example runs end-to-end and reports a
      compiled program.
- [ ] Rejected constructs produce precise messages naming the
      construct.
- [ ] `phaseC/test-matrix.md` updated.
- [ ] `phaseC_progress.md` appended.

## 10. Open questions

None blocking.
