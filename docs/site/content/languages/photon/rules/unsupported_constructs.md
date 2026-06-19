# Unsupported constructs (Python frontend)

The `@photon.kernel` decorator translates a Python AST into Phonon.
The translator is **deliberately small** — anything outside the
supported subset raises `UnsupportedConstructError` with a precise
diagnostic.

## What's accepted

See [`photon_kernel`](../reference/frontends/photon_kernel.md) — the
short answer is: `QReg`, gate methods, `photon.lib` calls, `for i in
range(lo, hi)`, `if cond == k`, `return q.measure_int()`.

## What's rejected (and why)

| Python | Why rejected |
|---|---|
| `while …` | unbounded loops not allowed (use `for`) |
| `import …` (inside the function) | imports are file-level only |
| `try / except` | exceptions don't lower to gates |
| `with …` | context managers don't lower |
| `def helper():` (nested) | nested function definitions not supported; use module-level |
| recursion | each call would generate a fresh inline; no termination |
| `print`, `assert` | I/O is forbidden in a kernel |
| `lambda x: …` | function values not lowerable |
| `yield`, generators | not lowerable |
| comprehensions / generator expressions | rejected (use `for`) |
| `async def`, `await` | not lowerable |
| list / dict / set literals | no runtime data structures |
| arbitrary attribute access (`q.foo` for unknown `foo`) | rejected with the offending name |
| `+`, `*`, `-` on `QReg` | not defined |

## Diagnostic shape

```
photon.UnsupportedConstructError: while statement is not supported
  in kernel 'bell' at line 7, col 5
  source: while q.depth() < 5: ...
```

The exception's `str()` is a **complete** message — no traceback
chasing.

## Where the check lives

[`photon/frontends/python/photon/_translator.py`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/python/photon/_translator.py)
walks every AST node and either translates or raises.

## See also

[`@photon.kernel`](../reference/frontends/photon_kernel.md),
[Install: photon Python](../../../languages/photon/install.md)
