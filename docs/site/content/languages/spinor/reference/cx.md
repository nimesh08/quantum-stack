# `cx` — controlled-NOT (CNOT)  *[two-qubit Clifford]*

Flip the target iff the control is `|1⟩`. The standard entangler.

## Synopsis

```
cx <control>, <target>
```

## Parameters

| name | type | constraint |
|---|---|---|
| control | qubit | declared, in range |
| target | qubit | declared, in range, **distinct** from control (W3) |

## Matrix (control = MSB, target = LSB)

$$\begin{pmatrix} 1 & 0 & 0 & 0 \\ 0 & 1 & 0 & 0 \\ 0 & 0 & 0 & 1 \\ 0 & 0 & 1 & 0 \end{pmatrix}$$

## Semantics

$|c, t\rangle \mapsto |c, t \oplus c\rangle$. The target is flipped (XOR'd
with the control). Identity to `H(t) cz(c,t) H(t)`.

## Legality

- W1, W2 — operands declared and in range.
- **W3** — control ≠ target. The no-cloning law surfaces here as a
  syntax-checker rule.
- W6 — under the hardware contract, the (control, target) pair must
  sit on a connected edge of `chip.coupling_map`. The router inserts
  `swap`s if needed.

## Examples

```spinor
qubit q[2]
h q[0]
cx q[0], q[1]                ; build a Bell pair
```

```spinor
qubit q[3]
h q[0]
cx q[0], q[1]
cx q[0], q[2]                ; 3-qubit GHZ
```

```spinor
qubit q[2]
cx q[0], q[0]                ; ERROR W3: distinct operands required
```

## Lowering

| Chip | `cx` becomes |
|---|---|
| IBM Heron r2 (native ECR) | `rz/sx` rotations + one `ecr` |
| IonQ Forte (native MS) | rotations + one `ms` |
| Quantinuum Helios (native RZZ + U1Q) | rotations + one `rzz(π/2)` |

The KAK decomposition guarantees ≤ 3 entanglers; in practice exactly
1 ECR / 1 MS / 1 RZZ (the chips have the right native gate set).

## Equivalents

- **Phonon**: identical.
- **Photon**: `q.cx(c, t)` or alias `q.cnot(c, t)`.

## See also

[`cz`](cz.md), [`swap`](swap.md), [`ecr`](ecr.md), [`ms`](ms.md),
[Cookbook: Bell program](../cookbook/bell.md),
[W3 distinct operands](../rules/W3_distinct_operands.md)
