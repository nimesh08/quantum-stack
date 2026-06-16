# Unsupported constructs

The `@photon.kernel` decorator translates a deliberately small Python
subset. Anything outside it raises `UnsupportedConstructError` with a
precise diagnostic.

## Supported

| Python | Maps to |
|---|---|
| `q = photon.QReg(N)` | `qubit q[N]; bit c[N]` |
| `q.h(0)`, `q.cx(0, 1)`, … | the matching Spinor gate |
| `q.bell_pair(0, 1)`, `q.ghz()`, etc. | `photon.lib` expansions |
| `for i in range(lo, hi): …` | counted `for i in lo..hi { … }` |
| `if cond == k: …` | `if (cond == k) { … }` |
| `return q.measure_int()` | kernel return |
| Integer literals, `pi`, simple arithmetic | folded |

## Rejected

| Python | Why |
|---|---|
| `while …` | unbounded; use `for` |
| `import …` | imports are file-scope only |
| `try/except/finally` | exceptions don't lower |
| `with …` | context managers don't lower |
| nested `def` | use module-level helpers |
| recursion (direct or mutual) | inlining wouldn't terminate |
| `print`, `assert` | I/O forbidden in a kernel |
| `lambda`, generator expressions, comprehensions | not lowerable |
| `async def`, `await` | not lowerable |
| list/dict/set literals | no runtime data structures |
| arbitrary attribute lookup (`q.unknown_method`) | rejected with the offending name |

## Diagnostic shape

```
photon.UnsupportedConstructError: while statement is not supported
  in kernel 'foo' at line 7, col 5
  source: while q.depth() < 5: ...
```

## See also

[`@photon.kernel`](../../languages/photon/reference/frontends/photon_kernel.md), [Decorator internals](kernel_decorator.md)
