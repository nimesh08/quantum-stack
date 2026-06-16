# `rx(θ)` — rotation around the X-axis  *[one-qubit continuous]*

Continuous rotation by angle θ around the X-axis of the Bloch sphere.

## Synopsis

```
rx ( <angle> ) <qubit>
```

## Parameters

| name | type | constraint |
|---|---|---|
| angle | `pi`, `pi/N`, `real`, `real*pi`, with optional unary `-` | finite |
| qubit | qubit operand | declared |

## Matrix

$$R_x(\theta) = \begin{pmatrix} \cos(\theta/2) & -i\sin(\theta/2) \\ -i\sin(\theta/2) & \cos(\theta/2) \end{pmatrix}$$

## Semantics

$|\psi\rangle \mapsto R_x(\theta)|\psi\rangle$. `rx(π) = -iX` (Pauli-X up to global phase).
`rx(0) = I`.

## Legality

W1, W2, W4. Legal under both contracts. The angle parameter must be
one of the four shapes (`pi`, `pi/N`, `real`, `real*pi`).

## Examples

```spinor
qubit q[1]
rx(pi)       q[0]            ; equivalent to x
rx(pi/2)     q[0]            ; quarter turn
rx(0.7853)   q[0]            ; raw radians
rx(-pi/4)    q[0]            ; unary minus
```

## Lowering

| Chip | `rx(θ)` becomes |
|---|---|
| IBM Heron r2 | virtual rotation: `rz(-π/2) sx rz(θ + π) sx rz(-π/2)` (Euler ZYZ) |
| IonQ Forte | `gpi2(θ)` family |
| Quantinuum Helios | `u1q(θ, 0)` |

## Equivalents

- **Phonon**: `rx(θ) q[0]`.
- **Photon**: `q.rx(θ, 0)`.

## See also

[`ry`](ry.md), [`rz`](rz.md), [`x`](x.md), [`h`](h.md)
