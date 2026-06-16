# `ry(θ)` — rotation around the Y-axis  *[one-qubit continuous]*

Continuous rotation by angle θ around the Y-axis.

## Synopsis

```
ry ( <angle> ) <qubit>
```

## Matrix

$$R_y(\theta) = \begin{pmatrix} \cos(\theta/2) & -\sin(\theta/2) \\ \sin(\theta/2) & \cos(\theta/2) \end{pmatrix}$$

Real entries — useful for variational ansätze.

## Semantics

`ry(π) = -iY`. `ry(π/2)` rotates `|0⟩` to `(|0⟩+|1⟩)/√2`.

## Legality

W1, W2, W4. Both contracts.

## Examples

```spinor
qubit q[1]
ry(pi)       q[0]            ; up to phase = y
ry(pi/2)     q[0]            ; like h on |0⟩, but real
```

## Lowering

Decomposes via the `decomposition.one_qubit.recipe` of the chip
(typically `euler_zyz`).

## Equivalents

- **Phonon**: `ry(θ) q[0]`.
- **Photon**: `q.ry(θ, 0)`.

## See also

[`rx`](rx.md), [`rz`](rz.md), [Cookbook: parameterized_gates](../cookbook/parameterized_gates.md)
