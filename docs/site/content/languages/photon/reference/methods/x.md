# `q.x(i)` — Photon X gate method  *[QReg method]*

OO façade for [Spinor's `x`](../../../spinor/reference/x.md).

## Synopsis

```photon
q.x(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`x`](../../../spinor/reference/x.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.x(0)
```

## Equivalents

- **Spinor**: `x q[0]`.
- **Phonon**: `x q[0]`.

## See also

[Spinor reference: `x`](../../../spinor/reference/x.md), [`QReg`](../QReg.md)
