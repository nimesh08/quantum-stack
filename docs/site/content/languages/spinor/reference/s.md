# `s` — phase gate (√Z)  *[one-qubit Clifford]*

Quarter-turn around the Z-axis. Two `s` gates compose to `z`.

## Synopsis

```
s <qubit>
```

## Matrix

$$\begin{pmatrix} 1 & 0 \\ 0 & i \end{pmatrix}$$

## Semantics

$|0\rangle \mapsto |0\rangle$, $|1\rangle \mapsto i|1\rangle$. Diagonal.
Equivalent to `rz(π/2)` up to global phase.

## Legality

W1, W2, W4. Legal under both contracts.

## Examples

```spinor
qubit q[1]
s q[0]
s q[0]                       ; equivalent to z
```

## Lowering

`s = rz(π/2)`. Native on most chips via virtual rz.

## Equivalents

- **Phonon**: `s q[0]`.
- **Photon**: `q.s(0)` or alias `q.phase(0)`.

## See also

[`sdg`](sdg.md), [`t`](t.md), [`z`](z.md), [`rz`](rz.md)
