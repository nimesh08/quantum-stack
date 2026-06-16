# `q.rz(theta, i)` — Photon RZ rotation method  *[QReg method]*

OO façade for [Spinor's `rz`](../../../spinor/reference/rz.md).

## Synopsis

```photon
q.rz(<angle>, <int>)
```

## Parameters

| name | type | constraint |
|---|---|---|
| theta | `angle` | `pi`, `pi/N`, `real`, `real*pi`, identifier ref, or `angle`-typed expression |
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply [`rz(theta)`](../../../spinor/reference/rz.md) to `q[i]`.

## Examples

```photon
QReg q(1)
q.rz(pi, 0)                  // equivalent to a Pauli rotation
q.rz(pi/4, 0)                // smaller turn
```

```photon
def parameterised(QReg q, angle theta) {
    q.rz(theta, 0)           // angle-typed parameter
}
```

## Equivalents

- **Spinor**: `rz(theta) q[0]`.
- **Phonon**: `rz(theta) q[0]`.

## See also

[Spinor reference: `rz`](../../../spinor/reference/rz.md), [`QReg`](../QReg.md)
