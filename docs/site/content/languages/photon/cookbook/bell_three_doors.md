# Bell, three doors

Same logical Bell program, written in all three Photon frontends. The
compiler produces **identical** Spinor for each.

## Door 1: `.pho`

```photon
target generic

kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}
```

Compile:

```bash
spinorc emit -t generic -f qasm3 bell.pho   # (or use the engine binding directly)
```

## Door 2: `@photon.kernel` (Python)

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.compiled.estimate())
print(bell.compiled.dump_spinor())
```

## Door 3: `[[photon::kernel]]` (C++)

```cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
```

Drive with `photonc-cxx`:

```yaml
build:
  sources: [bell.cpp]
  entry: bell_kernel
  target: generic
  shots: 1024
```

```bash
photonc-cxx build.yaml
```

## They produce identical Spinor

The convergence test asserts this. See
[three_door_convergence](../rules/three_door_convergence.md).

## Pick the door that fits your workflow

- **`.pho`** when you want a self-contained text artifact you can
  share / version / pipe through `git`.
- **Python** when you want to script multiple compilations (e.g.
  parameter sweeps).
- **C++** when your quantum kernel is one function inside a larger
  C++ codebase.

## See also

[Photon overview](../overview.md), [convergence rule](../rules/three_door_convergence.md)
