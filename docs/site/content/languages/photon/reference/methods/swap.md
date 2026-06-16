# `q.swap(a, b)` — swap two qubits  *[QReg method]*

OO façade for [Spinor's `swap`](../../../spinor/reference/swap.md). Costs
3 native two-qubit gates of error.

## Synopsis

```photon
q.swap(a, b)
```

## Examples

```photon
QReg q(3)
q.swap(0, 2)
```

## Equivalents

- **Spinor**: `swap q[0], q[2]`.

## See also

[Spinor `swap`](../../../spinor/reference/swap.md)
