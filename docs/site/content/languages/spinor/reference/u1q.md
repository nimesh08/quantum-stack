# `u1q(θ, φ)` — Quantinuum native single-qubit gate  *[native]*

A single-qubit rotation parameterised by two angles. Quantinuum's
native one-qubit primitive.

## Synopsis

```
u1q ( <theta>, <phi> ) <qubit>
```

## Matrix

A general SU(2) rotation determined by `(θ, φ)`. Concretely:
$U_{1Q}(\theta, \phi) = e^{-i\frac{\theta}{2}(\cos\phi\, X + \sin\phi\, Y)}$.

## Semantics

`u1q(π, 0) = X`, `u1q(π/2, 0) = √X`, `u1q(π, π/2) = Y`. Any
single-qubit gate is one `u1q` up to a `rz` (which is virtual).

## Legality

W1, W2, W4, W6. native_gate.

## Examples

```spinor
target quantinuum_helios
qubit q[1]
u1q(pi, 0)   q[0]            ; like x
u1q(pi/2, 0) q[0]            ; like sx
```

## Equivalents

- **Phonon**: hardware contract.
- **Photon**: `q.u1q(theta, phi, 0)`.

## See also

[`gpi`](gpi.md), [`rx`](rx.md), [`ry`](ry.md), [`rz`](rz.md)
