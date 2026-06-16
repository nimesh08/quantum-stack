# `q.cz(a, b)` — controlled-Z  *[QReg method]*

OO façade for [Spinor's `cz`](../../../spinor/reference/cz.md). Symmetric in
its two operands.

## Synopsis

```photon
q.cz(a, b)
```

## Examples

```photon
QReg q(2)
q.h(0)
q.cz(0, 1)
```

## Equivalents

- **Spinor**: `cz q[0], q[1]`.

## See also

[Spinor `cz`](../../../spinor/reference/cz.md), [`cx_cnot`](cx_cnot.md)
