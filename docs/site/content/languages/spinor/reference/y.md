# `y` — Pauli-Y  *[one-qubit Clifford]*

A bit-and-phase flip. Equivalent to `iXZ`.

## Synopsis

```
y <qubit>
```

## Matrix

$$\begin{pmatrix} 0 & -i \\ i & 0 \end{pmatrix}$$

## Semantics

$|0\rangle \mapsto i|1\rangle$, $|1\rangle \mapsto -i|0\rangle$.

## Legality

W1, W2, W4. Legal under both contracts.

## Examples

```spinor
qubit q[1]
h q[0]                       ; q[0] in |+⟩
y q[0]                       ; q[0] now in i|-⟩
```

## Lowering

`y = i · x · z`. On most chips: `rz(π) x rz(-π)` (up to global phase),
or more compactly `gpi(π/2)` on IonQ.

## Equivalents

- **Phonon**: `y q[0]`.
- **Photon**: `q.y(0)`.

## See also

[`x`](x.md), [`z`](z.md), [`ry`](ry.md)
