# `t` — T gate (eighth-turn)  *[one-qubit non-Clifford]*

The smallest standard non-Clifford gate. Eight `t` gates compose to
identity. Counted by `t_count` in the resource estimate — the dominant
cost in fault-tolerant accounting.

## Synopsis

```
t <qubit>
```

## Matrix

$$\begin{pmatrix} 1 & 0 \\ 0 & e^{i\pi/4} \end{pmatrix}$$

## Semantics

$|1\rangle \mapsto e^{i\pi/4}|1\rangle$. Equivalent to `rz(π/4)`.

## Legality

W1, W2, W4. Legal under both contracts.

`t` is **not** a Clifford gate — Stim cannot simulate circuits with
`t` in stabilizer mode. The general-purpose statevector simulator
handles it.

## Examples

```spinor
qubit q[1]
h q[0]
t q[0]                       ; π/4 phase on the |1⟩ component
```

## Lowering

`t = rz(π/4)`.

## Equivalents

- **Phonon**: `t q[0]`.
- **Photon**: `q.t(0)`.

## See also

[`tdg`](tdg.md), [`s`](s.md), [`rz`](rz.md)
