# `rzz(θ)` — Z⊗Z rotation  *[native two-qubit, parameterised]*

Continuous two-qubit interaction. The native entangler on
Quantinuum chips and on some IonQ generations.

## Synopsis

```
rzz ( <angle> ) <q1>, <q2>
```

## Matrix

$$R_{ZZ}(\theta) = e^{-i\theta Z\otimes Z / 2}$$

Diagonal with entries $e^{-i\theta/2}, e^{i\theta/2}, e^{i\theta/2}, e^{-i\theta/2}$.

## Semantics

Continuously parameterised entangler. `rzz(π/2)` is locally
equivalent to a CNOT.

## Legality

W1, W2, W3, W6. native_gate.

## Examples

```spinor
target quantinuum_helios
qubit q[2]
rzz(pi/2) q[0], q[1]         ; entangler
```

## Lowering

Fixed-point on Quantinuum/IonQ.

## Equivalents

- **Phonon**: hardware contract only.
- **Photon**: `q.rzz(theta, 0, 1)`.

## See also

[`ms`](ms.md), [`ecr`](ecr.md), [`rz`](rz.md), [`cx`](cx.md)
