# `q.sx(i)` and `q.sxdg(i)` — native one-qubit gates  *[QReg method]*

OO façade for [`sx`](../../../spinor/reference/sx.md) and
[`sxdg`](../../../spinor/reference/sxdg.md). Native on IBM chips;
illegal under `target generic`.

## Synopsis

```photon
q.sx(i)
q.sxdg(i)
```

## Examples

```photon
target ibm_heron_r2
QReg q(1)
q.sx(0)
q.sx(0)                      ; identity to q.x(0)
q.sxdg(0)                    ; inverse of sx
```

## Equivalents

- **Spinor**: `sx q[i]`, `sxdg q[i]`.

## See also

[Spinor `sx`](../../../spinor/reference/sx.md), [Spinor `sxdg`](../../../spinor/reference/sxdg.md)
