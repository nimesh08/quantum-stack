# `q.sdg(i)` — Photon SDG gate method  *[QReg method]*

OO façade for [Spinor's `sdg`](../../../spinor/reference/sdg.md).

## Synopsis

```photon
q.sdg(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`sdg`](../../../spinor/reference/sdg.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.sdg(0)
```

## Equivalents

- **Spinor**: `sdg q[0]`.
- **Phonon**: `sdg q[0]`.

## See also

[Spinor reference: `sdg`](../../../spinor/reference/sdg.md), [`QReg`](../QReg.md)
