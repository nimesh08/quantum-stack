# `q.z(i)` — Photon Z gate method  *[QReg method]*

OO façade for [Spinor's `z`](../../../spinor/reference/z.md).

## Synopsis

```photon
q.z(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`z`](../../../spinor/reference/z.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.z(0)
```

## Equivalents

- **Spinor**: `z q[0]`.
- **Phonon**: `z q[0]`.

## See also

[Spinor reference: `z`](../../../spinor/reference/z.md), [`QReg`](../QReg.md)
