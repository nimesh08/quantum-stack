# `gpi2(φ)` — IonQ native π/2 gate  *[native]*

A π/2 rotation around an axis in the X-Y plane.

## Synopsis

```
gpi2 ( <angle> ) <qubit>
```

## Matrix

$$\frac{1}{\sqrt{2}}\begin{pmatrix} 1 & -i e^{-i\phi} \\ -i e^{i\phi} & 1 \end{pmatrix}$$

## Semantics

`gpi2(0)` is the IonQ form of `√X`. `gpi2(π/2)` is `√Y` (up to global phase).
Hadamard becomes `gpi2(π/2)` directly.

## Legality

W1, W2, W4, W6. native_gate.

## Examples

```spinor
target ionq_forte
qubit q[1]
gpi2(pi/2) q[0]              ; equivalent to h (up to global phase)
```

## Equivalents

- **Phonon**: hardware contract.
- **Photon**: `q.gpi2(phi, 0)`.

## See also

[`gpi`](gpi.md), [`h`](h.md), [`rx`](rx.md)
