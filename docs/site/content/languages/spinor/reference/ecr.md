# `ecr` — Echoed Cross-Resonance  *[native, hardware contract only]*

IBM's native two-qubit entangler. Locally equivalent to a CNOT.

## Synopsis

```
ecr <q1>, <q2>
```

## Matrix

$$\frac{1}{\sqrt{2}}\begin{pmatrix} 0 & 0 & 1 & i \\ 0 & 0 & i & 1 \\ 1 & -i & 0 & 0 \\ -i & 1 & 0 & 0 \end{pmatrix}$$

## Semantics

A local-Clifford-equivalent of CNOT. After `ecr`, single-qubit
rotations rebuild the desired CX, CZ, or other two-qubit gate.

## Legality

W1, W2, W3, **W6 only** — `ecr` is a `native_gate`, illegal under
`target generic`. The pair must be on a connected edge.

## Examples

```spinor
target ibm_heron_r2
qubit q[2]
ecr q[17], q[18]             ; legal if (17,18) is in the heavy-hex coupling
```

## Lowering

`ecr` is the fixed-point — chips that have it native don't lower it
further. The KAK decomposition pass produces `ecr` from any
two-qubit gate when targeting an ECR chip.

## Equivalents

- **Phonon**: legal under hardware contract.
- **Photon**: `q.ecr(c, t)`.

## See also

[`cx`](cx.md), [`ms`](ms.md), [`rzz`](rzz.md),
[Cookbook: IBM native gates](../cookbook/ibm_native_gates.md)
