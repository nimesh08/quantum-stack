# `q.tdg(i)` — Photon TDG gate method  *[QReg method]*

OO façade for [Spinor's `tdg`](../../../spinor/reference/tdg.md).

## Synopsis

```photon
q.tdg(i)
```

## Parameters

| name | type | constraint |
|---|---|---|
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply the [`tdg`](../../../spinor/reference/tdg.md) gate to qubit `q[i]`.
After the call, `q[i]` is updated; other slots unchanged.

## Examples

```photon
QReg q(1)
q.tdg(0)
```

## Equivalents

- **Spinor**: `tdg q[0]`.
- **Phonon**: `tdg q[0]`.

## See also

[Spinor reference: `tdg`](../../../spinor/reference/tdg.md), [`QReg`](../QReg.md)
