# Arbitrary single-qubit rotation

## What it does

Build any single-qubit unitary as three rotations: ZYZ Euler decomposition.

## Recipe

```spinor
target generic
qubit q[1]
rz(alpha) q[0]
ry(beta)  q[0]
rz(gamma) q[0]
```

For specific gates:

| Gate | (α, β, γ) |
|---|---|
| H | (π/2, π/2, π/2) |
| X | (0, π, 0) |
| Y | (π, π, 0) |
| Z | (0, 0, π) |
| S | (0, 0, π/2) |
| T | (0, 0, π/4) |

## Why it works

Any 2×2 unitary up to global phase can be written as
$R_z(\gamma) R_y(\beta) R_z(\alpha)$. The compiler uses the same
identity to produce rotations on chips that have native rz + native sx
(IBM) or native u1q (Quantinuum).

## Variations

- **XYX form**: `rx ry rx`. Same span.
- **Restricted to {sx, rz}** (IBM Heron): `rz/sx/rz/sx/rz` covers
  every single-qubit unitary (Bloch-sphere rotation).

## Same in Phonon

Identical.

## Same in Photon

```photon
QReg q(1)
q.rz(alpha, 0)
q.ry(beta, 0)
q.rz(gamma, 0)
```

## Side effects on cost

3 single-qubit gates. With virtual rz, only the ry counts at runtime
on most chips (rz is bookkeeping).

## Where to look

- Reference: [`rx`](../reference/rx.md), [`ry`](../reference/ry.md), [`rz`](../reference/rz.md)
- Cookbook: [parameterized_gates](parameterized_gates.md)
