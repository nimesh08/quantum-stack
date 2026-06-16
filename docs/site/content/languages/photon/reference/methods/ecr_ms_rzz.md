# `q.ecr(a, b)`, `q.ms(a, b)`, `q.rzz(theta, a, b)` — native two-qubit gates  *[QReg method]*

OO façade for [`ecr`](../../../spinor/reference/ecr.md) (IBM),
[`ms`](../../../spinor/reference/ms.md) (IonQ), and
[`rzz`](../../../spinor/reference/rzz.md) (Quantinuum / IonQ).

All three are **native_gate** — illegal under `target generic`. Used
mostly by inspection / testing of the chip-locked output.

## Synopsis

```photon
q.ecr(a, b)
q.ms(a, b)
q.rzz(theta, a, b)
```

## Examples

```photon
target ibm_heron_r2
QReg q(2)
q.ecr(0, 1)
```

```photon
target ionq_forte
QReg q(2)
q.ms(0, 1)
```

```photon
target quantinuum_helios
QReg q(2)
q.rzz(pi/2, 0, 1)
```

## Equivalents

- **Spinor**: hardware contract.

## See also

[Spinor `ecr`](../../../spinor/reference/ecr.md), [`ms`](../../../spinor/reference/ms.md), [`rzz`](../../../spinor/reference/rzz.md)
