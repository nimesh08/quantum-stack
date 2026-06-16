# `photon` Python package

The Phase C Python frontend. Provides the `@photon.kernel` decorator,
the `photon.QReg` helper, and a thin entry to the C++ engine.

## Install

The package lives in the monorepo at
[`photon/frontends/python/`](https://github.com/nimesh08/quantum-stack/tree/main/photon/frontends/python).
Install editably:

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install nanobind                     # required by the binding
pip install -e photon/frontends/python
```

If you've already built the engine from source (see
[Install from source](from_source.md)), the nanobind extension
`photon._engine` is built into `build/photon/bindings/python/`. Add
that to `PYTHONPATH`:

```bash
export PYTHONPATH="$PWD/build/photon/bindings/python:$PYTHONPATH"
```

If the extension isn't built (host without LLVM), the package still
imports — `_ENGINE_AVAILABLE` is False and `compile_phonon` raises
with a clear message. The decorator itself does not require the
engine to import; it only needs it at compile time.

## Smoke test

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.phonon_text)        # the lowered Phonon source
print(bell.compiled.estimate().num_qubits)   # 2
print(bell.run(shots=1000))    # {'00': 1000} (stub)
```

## Public surface

- `@photon.kernel` — the decorator.
- `photon.QReg(n)` — quantum register helper.
- `photon.compile_phonon(text, target='generic')` — direct engine call.
- `photon.UnsupportedConstructError` — raised for unsupported AST nodes.
- `photon.CompilationError` — raised for engine errors.

The decorator's full reference is in
[Photon Python frontend](../languages/photon/reference/frontends/photon_kernel.md).

## What it accepts

The Python AST visitor maps a small subset of Python onto Phonon:

| Python | Maps to |
|---|---|
| `q = photon.QReg(N)` | `qubit q[N]; bit c[N]` declaration |
| `q.h(0)`, `q.cx(0, 1)`, … | the matching gate |
| `for i in range(lo, hi): …` | counted `for i in lo..hi { … }` |
| `if cond == k: …` | `if (cond == k) { … }` |
| `return q.measure_int()` | `bit c[N] = measure q; return c` |

Anything else raises `UnsupportedConstructError` with the AST node
name and the line/column.

## See also

- [`@photon.kernel` reference](../languages/photon/reference/frontends/photon_kernel.md)
- [Unsupported constructs](../languages/photon/rules/unsupported_constructs.md)
- [Photon Python cookbook](../packages/photon_python/cookbook.md)
