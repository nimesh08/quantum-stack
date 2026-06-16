# Using aliases — `hadamard`, `cnot`, `phase`

## What it does

Photon ships three convenience aliases that map to the canonical
gate names. Useful for code lifted from Qiskit/Cirq textbooks.

## Recipe

```photon
QReg q(2)

q.hadamard(0)                ; identical to q.h(0)
q.cnot(0, 1)                 ; identical to q.cx(0, 1)
q.phase(0)                   ; identical to q.s(0)
```

## Mixed-style is fine

```photon
QReg q(2)
q.h(0)                       ; canonical
q.cnot(0, 1)                 ; alias
```

The compiler treats them as identical at the lexer level (see
[`Lexer.cpp` line 70-72](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Lexer.cpp#L70-L72)).
The compiled Spinor is identical regardless of which spelling you use.

## In all three frontends

```python
@photon.kernel
def bell():
    q = photon.QReg(2)
    q.hadamard(0)
    q.cnot(0, 1)
    return q.measure_int()
```

```cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.hadamard(0);
  q.cnot(0, 1);
  return q.measure_int();
}
```

## See also

[Aliases reference](../reference/methods/aliases.md), [Three doors](../rules/three_door_convergence.md)
