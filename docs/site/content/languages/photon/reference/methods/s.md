# `q.s(i)` — Photon S gate method  *[QReg method]*

OO façade for [Spinor's `s`](../../../spinor/reference/s.md).

## Synopsis

```photon
q.s(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`s`](../../../spinor/reference/s.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.s(0)
```

## Equivalents

- **Spinor**: `s q[0]`.
- **Phonon**: `s q[0]`.

## See also

[Spinor reference: `s`](../../../spinor/reference/s.md), [`QReg`](../QReg.md)
