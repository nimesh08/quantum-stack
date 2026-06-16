# `@photon.kernel` — Python frontend  *[frontend]*

Python decorator. Reads the function's AST, maps it to Phonon, and
compiles via `photon._engine`.

## Synopsis

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
```

## What's accepted

| Python | Maps to |
|---|---|
| `q = photon.QReg(N)` | `QReg q(N)` |
| `q.h(0)`, `q.cx(0, 1)`, … | the matching method |
| `q.bell_pair(0, 1)`, `q.ghz()`, … | photon.lib expansions |
| `for i in range(lo, hi): …` | counted `for i in lo..hi { … }` |
| `if cond == k: …` | `if (cond == k) { … }` |
| `return q.measure_int()` | kernel return |

## What's rejected (`UnsupportedConstructError`)

`while`, `import`, `try`, `with`, recursion, free-function calls
outside `photon.lib`, `print`, `assert`, list/dict/set literals,
generators, comprehensions, `lambda`, `yield`, async — see
[unsupported constructs](../../rules/unsupported_constructs.md).

The error names the offending node and the line/column.

## Decorator API

```python
bell.phonon_text                     # the lowered Phonon source as a string
bell.compiled                        # photon._engine.CompiledProgram
bell.compiled.estimate()             # ResourceEstimate
bell.compiled.dump_spinor()          # Spinor IR text
bell.compiled.error                  # diagnostic if ok=False
bell.run(shots=1000, target="ibm_heron_r2")
                                     # submit through the Phase A adapters
```

## Source

[`photon/frontends/python/photon/_decorator.py`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/python/photon/_decorator.py),
[`_translator.py`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/python/photon/_translator.py).

## See also

[`pho_file`](pho_file.md), [`cpp_attribute`](cpp_attribute.md),
[Install: photon Python](../../../../install/photon_python.md)
