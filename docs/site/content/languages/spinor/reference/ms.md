# `ms` — Mølmer–Sørensen gate  *[native, hardware contract only]*

IonQ's native two-qubit entangler. The trapped-ion analogue of ECR.

## Synopsis

```
ms <q1>, <q2>
```

## Matrix

$$\frac{1}{\sqrt{2}} \begin{pmatrix} 1 & 0 & 0 & -i \\ 0 & 1 & -i & 0 \\ 0 & -i & 1 & 0 \\ -i & 0 & 0 & 1 \end{pmatrix}$$

## Semantics

`ms` couples two ions through a shared phonon mode. With surrounding
single-qubit rotations it implements any two-qubit gate.

## Legality

W1, W2, W3, W6. Native_gate — illegal under `target generic`.

IonQ chips are typically all-to-all — any pair is "connected".

## Examples

```spinor
target ionq_forte
qubit q[3]
ms q[0], q[2]                ; all-to-all: any pair is fine
```

## Lowering

Fixed-point on IonQ chips. The KAK pass produces it.

## Equivalents

- **Phonon**: hardware contract only.
- **Photon**: `q.ms(0, 2)`.

## See also

[`cx`](cx.md), [`rzz`](rzz.md), [`gpi`](gpi.md),
[Cookbook: IonQ native gates](../cookbook/ionq_native_gates.md)
