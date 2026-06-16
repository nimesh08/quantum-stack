# IonQ native gate set

## What it does

Show what programs look like compiled for IonQ Forte. Native
vocabulary: `ms rzz gpi gpi2 u1q` (the trapped-ion family).

## Recipe (Bell, after compiling for IonQ)

```spinor
target ionq_forte
qubit q[2]
gpi2(pi/2) q[0]              ; from h
ms q[0], q[1]                ; from cx (the trapped-ion entangler)
gpi2(0) q[0]
gpi2(pi/2) q[1]
```

Forte has 36 ions in an all-to-all chain, so any pair can interact —
**no routing needed**.

## Why it works

`ms` couples two ions through a shared phonon (motional) mode. The
gate is locally-Clifford-equivalent to CX; with surrounding `gpi2`
rotations the compiler produces the desired two-qubit gate. KAK
decomposition guarantees ≤1 `ms` per `cx`.

## Variations

- **All-to-all**: any two qubits can interact. The router has nothing
  to do; placement is trivial.
- **`rzz` instead of `ms`**: some IonQ generations expose a parametric
  `rzz` that interpolates between identity and `cx`.

## Same in Phonon

Same. The optimizer runs once; chip-specific lowering is per-target.

## Same in Photon

Same.

## Side effects on cost

| Item | Value |
|---|---|
| `pricing.per_shot_usd` | 0.01 |
| Bell @ 1000 shots | $10.000 |
| typical 2-qubit error | ~0.3% (better than superconducting) |

Trapped ions are slow but high-fidelity.

## Where to look

- Chip YAML: [`spinor/registry/chips/ionq_forte.yaml`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/registry/chips/ionq_forte.yaml)
- Reference: [`ms`](../reference/ms.md), [`gpi`](../reference/gpi.md), [`gpi2`](../reference/gpi2.md)
