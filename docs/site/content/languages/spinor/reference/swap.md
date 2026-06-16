# `swap` — swap two qubits  *[two-qubit Clifford]*

Exchanges the full state of two qubits. Costs **three** native
two-qubit entanglers — used by the router to move a logical qubit
across the chip.

## Synopsis

```
swap <q1>, <q2>
```

## Matrix

$$\begin{pmatrix} 1 & 0 & 0 & 0 \\ 0 & 0 & 1 & 0 \\ 0 & 1 & 0 & 0 \\ 0 & 0 & 0 & 1 \end{pmatrix}$$

## Semantics

$|a, b\rangle \mapsto |b, a\rangle$. Equivalent to three CNOTs:
`cx(a,b); cx(b,a); cx(a,b)`.

## Legality

W1, W2, W3. W6 connected pair.

## Examples

```spinor
qubit q[3]
swap q[0], q[2]              ; q[0] state ends up at q[2], and vice versa
```

## Lowering

Each `swap` becomes 3 native entanglers (3× the error of one `cx`).
The SABRE router minimises the number of swaps to land all gates on
connected pairs.

## Equivalents

- **Phonon**: `swap q[0], q[1]`.
- **Photon**: `q.swap(0, 1)`.

## See also

[`cx`](cx.md), [Cookbook: routing via swap](../cookbook/routing_via_swap.md)
