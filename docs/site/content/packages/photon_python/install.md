# Install — `photon` Python package

The Phase C Python frontend. Provides the `@photon.kernel` decorator,
the `photon.QReg` helper, and a thin entry to the C++ engine.

## Quickstart

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
python3.12 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install nanobind                     # required by the binding
pip install -e photon/frontends/python
```

If you've also built the engine from source (see
[Install from source](../../install/from_source.md)), the `photon._engine`
extension is at `build/photon/bindings/python/`. Add it to PYTHONPATH:

```bash
export PYTHONPATH="$PWD/build/photon/bindings/python:$PYTHONPATH"
```

## Smoke test

```python
import photon
print(photon.__version__)        # 0.3.0+phasec.m3 or later

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.compiled.estimate())
```

## See also

- [Reference](reference.md) — every public symbol
- [Decorator semantics](kernel_decorator.md)
- [Unsupported constructs](unsupported_constructs.md)
- [Cookbook](cookbook.md)
