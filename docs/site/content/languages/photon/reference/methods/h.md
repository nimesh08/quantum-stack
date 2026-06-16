# `q.h(i)` — Photon H gate method  *[QReg method]*

OO façade for [Spinor's `h`](../../../spinor/reference/h.md).

## Synopsis

```photon
q.h(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`h`](../../../spinor/reference/h.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.h(0)
```

## Equivalents

- **Spinor**: `h q[0]`.
- **Phonon**: `h q[0]`.

## See also

[Spinor reference: `h`](../../../spinor/reference/h.md), [`QReg`](../QReg.md)
