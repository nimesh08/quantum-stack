# `gpi(φ)` — IonQ native one-qubit gate  *[native]*

A π rotation around an axis in the X-Y plane parameterised by φ.

## Synopsis

```
gpi ( <angle> ) <qubit>
```

## Matrix

$$\begin{pmatrix} 0 & e^{-i\phi} \\ e^{i\phi} & 0 \end{pmatrix}$$

## Semantics

`gpi(0) = X`, `gpi(π/2) = Y`. Self-inverse. The IonQ workhorse.

## Legality

W1, W2, W4, W6. native_gate.

## Examples

```spinor
target ionq_forte
qubit q[1]
gpi(0)    q[0]               ; equivalent to x
gpi(pi/2) q[0]               ; equivalent to y
```

## Equivalents

- **Phonon**: hardware contract.
- **Photon**: `q.gpi(phi, 0)`.

## See also

[`gpi2`](gpi2.md), [`x`](x.md), [`y`](y.md)
