# `sdg` — S-dagger (inverse of `s`)  *[one-qubit Clifford]*

The inverse of [`s`](s.md). Quarter-turn around the Z-axis the other way.

## Synopsis

```
sdg <qubit>
```

## Matrix

$$\begin{pmatrix} 1 & 0 \\ 0 & -i \end{pmatrix}$$

## Semantics

$|1\rangle \mapsto -i|1\rangle$. Equivalent to `rz(-π/2)` up to global phase.

## Legality

W1, W2, W4. Legal under both contracts.

## Examples

```spinor
qubit q[1]
s   q[0]
sdg q[0]                     ; identity
```

## Lowering

`sdg = rz(-π/2)`.

## Equivalents

- **Phonon**: `sdg q[0]`.
- **Photon**: `q.sdg(0)`.

## See also

[`s`](s.md), [`tdg`](tdg.md), [`rz`](rz.md)
