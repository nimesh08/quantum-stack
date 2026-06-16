# `q.ry(theta, i)` — Photon RY rotation method  *[QReg method]*

OO façade for [Spinor's `ry`](../../../spinor/reference/ry.md).

## Synopsis

```photon
q.ry(<angle>, <int>)
```

## Parameters

| name | type | constraint |
|---|---|---|
| theta | `angle` | `pi`, `pi/N`, `real`, `real*pi`, identifier ref, or `angle`-typed expression |
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply [`ry(theta)`](../../../spinor/reference/ry.md) to `q[i]`.

## Examples

```photon
QReg q(1)
q.ry(pi, 0)                  // equivalent to a Pauli rotation
q.ry(pi/4, 0)                // smaller turn
```

```photon
def parameterised(QReg q, angle theta) {
    q.ry(theta, 0)           // angle-typed parameter
}
```

## Equivalents

- **Spinor**: `ry(theta) q[0]`.
- **Phonon**: `ry(theta) q[0]`.

## See also

[Spinor reference: `ry`](../../../spinor/reference/ry.md), [`QReg`](../QReg.md)
