# `q.hadamard`, `q.cnot`, `q.phase` — aliases  *[QReg methods]*

Friendly aliases for `q.h`, `q.cx`, and `q.s`. The compiler treats
them identically; same Spinor output.

## Synopsis

```photon
q.hadamard(i)                ; alias for q.h(i)
q.cnot(c, t)                 ; alias for q.cx(c, t)
q.phase(i)                   ; alias for q.s(i)
```

## Why aliases?

Some textbooks / Qiskit programs use `cnot` and `hadamard`. The
aliases save a translation step. The lexer recognises them at
[`Lexer.cpp` line 70-72](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Lexer.cpp#L70-L72).

## Examples

```photon
QReg q(2)
q.hadamard(0)
q.cnot(0, 1)                 ; identical compile output to q.h(0); q.cx(0,1)
```

## Three-door convergence

The Python frontend (`@photon.kernel`) and the C++ frontend
(`[[photon::kernel]]`) accept the aliases identically. The
[convergence test](../../rules/three_door_convergence.md) ensures all
three produce the same Spinor.

## See also

[`h`](h.md), [`cx_cnot`](cx_cnot.md), [`s`](s.md)
