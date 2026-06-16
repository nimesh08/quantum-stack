# `sx` — √X (square-root of X)  *[native one-qubit]*

IBM's native one-qubit gate. Two `sx` compose to `x`.

## Synopsis

```
sx <qubit>
```

## Matrix

$$\frac{1}{2}\begin{pmatrix} 1+i & 1-i \\ 1-i & 1+i \end{pmatrix}$$

## Semantics

Half a bit-flip; equivalent to `rx(π/2)` up to global phase.

## Legality

W1, W2, W4, W6. `native_gate` — illegal under `target generic`.

## Examples

```spinor
target ibm_heron_r2
qubit q[1]
sx q[0]
sx q[0]                      ; identity to x q[0]
```

## Lowering

Fixed-point on IBM. `h` decomposes to `rz(π/2) sx rz(π/2)` via this gate.

## Equivalents

- **Phonon**: hardware contract.
- **Photon**: `q.sx(0)`.

## See also

[`sxdg`](sxdg.md), [`x`](x.md), [`rx`](rx.md), [`h`](h.md)
