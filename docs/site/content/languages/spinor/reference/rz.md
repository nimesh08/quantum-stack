# `rz(θ)` — rotation around the Z-axis  *[one-qubit continuous]*

Continuous rotation by angle θ around the Z-axis. Diagonal — does
not change measurement probabilities, only relative phase.

## Synopsis

```
rz ( <angle> ) <qubit>
```

## Matrix

$$R_z(\theta) = \begin{pmatrix} e^{-i\theta/2} & 0 \\ 0 & e^{i\theta/2} \end{pmatrix}$$

## Semantics

Phase rotation. `rz(π) = -iZ`, `rz(π/2) = e^{-iπ/4} S`, `rz(π/4) ≈ T`.

On most modern chips `rz` is **virtual** — the compiler tracks it as
phase bookkeeping and never emits a physical pulse. Free in error.

## Legality

W1, W2, W4. Both contracts.

## Examples

```spinor
qubit q[1]
rz(pi)        q[0]           ; equivalent to z
rz(pi/4)      q[0]           ; equivalent to t
rz(-pi/2)     q[0]           ; equivalent to sdg
```

## Lowering

Native on every supported chip. Counted as 0 cost in many resource
models because the compiler tracks it virtually.

## Equivalents

- **Phonon**: `rz(θ) q[0]`.
- **Photon**: `q.rz(θ, 0)`.

## See also

[`s`](s.md), [`t`](t.md), [`z`](z.md), [`rx`](rx.md), [`ry`](ry.md)
