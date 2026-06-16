# `q.y(i)` — Photon Y gate method  *[QReg method]*

OO façade for [Spinor's `y`](../../../spinor/reference/y.md).

## Synopsis

```photon
q.y(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`y`](../../../spinor/reference/y.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.y(0)
```

## Equivalents

- **Spinor**: `y q[0]`.
- **Phonon**: `y q[0]`.

## See also

[Spinor reference: `y`](../../../spinor/reference/y.md), [`QReg`](../QReg.md)
