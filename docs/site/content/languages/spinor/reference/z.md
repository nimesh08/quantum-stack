# `z` — Pauli-Z (phase flip)  *[one-qubit Clifford]*

The phase flip. `|0⟩` unchanged, `|1⟩` gets a `-1` phase.

## Synopsis

```
z <qubit>
```

## Matrix

$$\begin{pmatrix} 1 & 0 \\ 0 & -1 \end{pmatrix}$$

## Semantics

$|0\rangle \mapsto |0\rangle$, $|1\rangle \mapsto -|1\rangle$. Diagonal in the
computational basis — it does not change measurement probabilities,
only the relative phase of superpositions.

## Legality

W1, W2, W4. Legal under both contracts.

## Examples

```spinor
qubit q[1]
h q[0]                       ; q[0] in (|0⟩+|1⟩)/√2
z q[0]                       ; now (|0⟩-|1⟩)/√2  (the |-⟩ state)
h q[0]                       ; collapses to |1⟩
```

## Lowering

`z = rz(π)` (up to global phase). On IBM `rz(π)`; on IonQ `gpi2(0) gpi(π/2) gpi2(0)`
or equivalently a single `rz(π)` since IonQ supports virtual `rz`.

## Equivalents

- **Phonon**: `z q[0]`.
- **Photon**: `q.z(0)`.

## See also

[`x`](x.md), [`y`](y.md), [`s`](s.md), [`t`](t.md), [`rz`](rz.md)
