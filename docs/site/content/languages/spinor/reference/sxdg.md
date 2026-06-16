# `sxdg` — inverse of `sx`  *[native one-qubit]*

The inverse of [`sx`](sx.md). Equivalent to `rx(-π/2)`.

## Synopsis

```
sxdg <qubit>
```

## Matrix

$$\frac{1}{2}\begin{pmatrix} 1-i & 1+i \\ 1+i & 1-i \end{pmatrix}$$

## Legality

W1, W2, W4, W6. native_gate.

## Examples

```spinor
target ibm_heron_r2
qubit q[1]
sx   q[0]
sxdg q[0]                    ; identity
```

## Equivalents

- **Phonon**: hardware contract.
- **Photon**: `q.sxdg(0)`.

## See also

[`sx`](sx.md), [`rx`](rx.md)
