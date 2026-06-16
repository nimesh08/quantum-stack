# `q.rx(theta, i)` — Photon RX rotation method  *[QReg method]*

OO façade for [Spinor's `rx`](../../../spinor/reference/rx.md).

## Synopsis

```photon
q.rx(<angle>, <int>)
```

## Parameters

| name | type | constraint |
|---|---|---|
| theta | `angle` | `pi`, `pi/N`, `real`, `real*pi`, identifier ref, or `angle`-typed expression |
| i | `int` | `0 <= i < q.size()` |

## Semantics

Apply [`rx(theta)`](../../../spinor/reference/rx.md) to `q[i]`.

## Examples

```photon
QReg q(1)
q.rx(pi, 0)                  // equivalent to a Pauli rotation
q.rx(pi/4, 0)                // smaller turn
```

```photon
def parameterised(QReg q, angle theta) {
    q.rx(theta, 0)           // angle-typed parameter
}
```

## Equivalents

- **Spinor**: `rx(theta) q[0]`.
- **Phonon**: `rx(theta) q[0]`.

## See also

[Spinor reference: `rx`](../../../spinor/reference/rx.md), [`QReg`](../QReg.md)
