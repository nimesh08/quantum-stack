# `tdg` — T-dagger (inverse of `t`)  *[one-qubit non-Clifford]*

The inverse of [`t`](t.md). Equivalent to `rz(-π/4)`.

## Synopsis

```
tdg <qubit>
```

## Matrix

$$\begin{pmatrix} 1 & 0 \\ 0 & e^{-i\pi/4} \end{pmatrix}$$

## Semantics

$|1\rangle \mapsto e^{-i\pi/4}|1\rangle$.

## Legality

W1, W2, W4. Legal under both contracts. Counted by `t_count`.

## Examples

```spinor
qubit q[1]
t   q[0]
tdg q[0]                     ; identity
```

## Lowering

`tdg = rz(-π/4)`.

## Equivalents

- **Phonon**: `tdg q[0]`.
- **Photon**: `q.tdg(0)`.

## See also

[`t`](t.md), [`sdg`](sdg.md), [`rz`](rz.md)
