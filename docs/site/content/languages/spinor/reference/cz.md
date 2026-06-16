# `cz` — controlled-Z  *[two-qubit Clifford]*

Apply a Z to the target iff the control is `|1⟩`. Symmetric in its
two operands (control and target are interchangeable).

## Synopsis

```
cz <q1>, <q2>
```

## Matrix

$$\text{diag}(1, 1, 1, -1)$$

## Semantics

$|11\rangle \mapsto -|11\rangle$. All other basis states unchanged. Equivalent
to `H(t) cx(c,t) H(t)`.

## Legality

W1, W2, W3 (distinct). W6 connected pair.

## Examples

```spinor
qubit q[2]
h q[0]
cz q[0], q[1]                ; phase entangler
```

## Lowering

Often more compact than `cx` on native-RZZ chips — no extra rotations
needed for the basis change.

## Equivalents

- **Phonon**: `cz q[0], q[1]`.
- **Photon**: `q.cz(0, 1)`.

## See also

[`cx`](cx.md), [`swap`](swap.md), [`rzz`](rzz.md)
