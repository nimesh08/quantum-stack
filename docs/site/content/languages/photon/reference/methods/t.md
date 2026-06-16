# `q.t(i)` — Photon T gate method  *[QReg method]*

OO façade for [Spinor's `t`](../../../spinor/reference/t.md).

## Synopsis

```photon
q.t(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`t`](../../../spinor/reference/t.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.t(0)
```

## Equivalents

- **Spinor**: `t q[0]`.
- **Phonon**: `t q[0]`.

## See also

[Spinor reference: `t`](../../../spinor/reference/t.md), [`QReg`](../QReg.md)
