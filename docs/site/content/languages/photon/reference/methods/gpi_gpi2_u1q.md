# `q.gpi(phi, i)`, `q.gpi2(phi, i)`, `q.u1q(theta, phi, i)` — native one-qubit gates  *[QReg method]*

OO façade for [`gpi`](../../../spinor/reference/gpi.md) (IonQ),
[`gpi2`](../../../spinor/reference/gpi2.md) (IonQ),
[`u1q`](../../../spinor/reference/u1q.md) (Quantinuum). All
**native_gate**.

## Synopsis

```photon
q.gpi(phi, i)
q.gpi2(phi, i)
q.u1q(theta, phi, i)
```

## Examples

```photon
target ionq_forte
QReg q(1)
q.gpi(0, 0)                  ; equivalent to x
q.gpi2(pi/2, 0)              ; equivalent to h
```

```photon
target quantinuum_helios
QReg q(1)
q.u1q(pi, 0, 0)              ; equivalent to x
```

## See also

[Spinor `gpi`](../../../spinor/reference/gpi.md), [`gpi2`](../../../spinor/reference/gpi2.md), [`u1q`](../../../spinor/reference/u1q.md)
