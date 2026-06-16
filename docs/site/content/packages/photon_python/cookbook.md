# Cookbook — `photon` Python package

Quick recipes using `@photon.kernel` and `photon.QReg` from Python.

## 1. Bell

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

print(bell.compiled.estimate())   # ResourceEstimate(num_qubits=2, ...)
```

## 2. GHZ via the library

```python
@photon.kernel
def ghz5():
    q = photon.QReg(5)
    q.ghz()                       # h + chained cx
    return q.measure_int()
```

## 3. Counted loop

```python
@photon.kernel
def fanout(n_pulses):
    q = photon.QReg(2)
    q.h(0)
    for i in range(0, n_pulses):
        q.rz(0.1, 0)
    q.cx(0, 1)
    return q.measure_int()
```

`n_pulses` must be a constant at the call site (the unroll happens
during translation).

## 4. Submit through the platform

```python
@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()

result = bell.run(shots=1000, target="ibm_heron_r2")
print(result)   # {'00': 498, '11': 502}  (cassette mode)
```

## 5. Compile-only (no submission)

```python
text = "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n"
prog = photon.compile_phonon(text, target="generic")
print(prog.dump_spinor())
print(prog.estimate())
```

## 6. Catch an unsupported construct

```python
import photon

try:
    @photon.kernel
    def bad():
        q = photon.QReg(1)
        while True:                # while is rejected
            q.h(0)
        return q.measure_int()
except photon.UnsupportedConstructError as e:
    print("rejected:", e)
```

## 7. Inspect what gets compiled

```python
print(bell.phonon_text)            # the lowered Phonon source
print(bell.compiled.dump_phonon()) # post-optimizer Phonon
print(bell.compiled.dump_spinor()) # final Spinor IR
```

## 8. Multi-target sweep

```python
text = bell.phonon_text
for target in ("generic", "ibm_heron_r2", "ionq_forte"):
    prog = photon.compile_phonon(text, target=target)
    e = prog.estimate()
    print(f"{target:20s}  qubits={e.num_qubits}  2Q={e.two_qubit_count}  depth={e.depth}")
```

## See also

- [Reference](reference.md), [Decorator internals](kernel_decorator.md)
- [Photon language manual](../../languages/photon/index.md)
