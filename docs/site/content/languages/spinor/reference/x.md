# `x` — Pauli-X (quantum NOT)  *[one-qubit Clifford]*

The bit-flip. `|0⟩ ↔ |1⟩`. Equivalent to a 180° rotation around
the X-axis, up to global phase. Self-inverse.

## Synopsis

```
x <qubit>
```

## Matrix

$$\begin{pmatrix} 0 & 1 \\ 1 & 0 \end{pmatrix}$$

## Semantics

$|0\rangle \leftrightarrow |1\rangle$. Acts as classical NOT on a basis state.

## Legality

W1, W2, W4. Legal under both contracts (it's a standard gate).

## Examples

```spinor
qubit q[1]
x q[0]                       ; q[0] now |1⟩

x q[0]                       ; back to |0⟩ (X is self-inverse)
```

## Lowering

| Chip native gate set | `x` decomposes to |
|---|---|
| IBM Heron r2 (has `x` natively) | `x` |
| IonQ Forte | `gpi(0)` |
| Quantinuum Helios | `u1q(π, 0)` |

## Equivalents

- **Phonon**: `x q[0]`.
- **Photon**: `q.x(0)`.

## See also

[`y`](y.md), [`z`](z.md), [`h`](h.md), [`rx`](rx.md)
