# IBM native gate set

## What it does

Show what programs look like once compiled for IBM Heron r2. The
chip's native vocabulary is `ecr rz sx x` plus `rzz` for IsingZZ
operations (Heron r2 specific).

## Recipe (Bell, after compiling for IBM)

```spinor
target ibm_heron_r2
qubit q[2]
sx q[17]                     ; from h decomposition
rz(pi/2) q[17]
ecr q[17], q[18]             ; the cx becomes one ECR with surrounding rotations
sx q[18]
rz(-pi/2) q[18]
```

(Indices 17, 18 chosen by the placement pass from last night's
calibration data.)

## Why it works

IBM's transmon qubits implement `sx` (a √X pulse) and `ecr` (echoed
cross-resonance) directly. Every other gate decomposes to combinations
of these plus virtual `rz`. KAK decomposition guarantees `cx` becomes
exactly one `ecr` plus rotations.

## Variations

- **`x` is also native** on Heron r2 — saves a `sx sx` for plain X.
- **`rz` is virtual** — no physical pulse, no time, no error.
- **Heavy-hex coupling**: not every pair of qubits is connected; the
  router inserts `swap`s. See [routing_via_swap](routing_via_swap.md).

## Same in Phonon

Same. Phonon's optimizer runs **before** Spinor's chip-locking, so
your Phonon code is portable.

## Same in Photon

Same. The compiler does the lowering — you don't write `ecr` by hand.

## Side effects on cost

| Item | Value |
|---|---|
| `pricing.per_shot_usd` | 0.00033 |
| Bell @ 1000 shots | $0.330 |
| typical 2-qubit error | ~1% |

## Where to look

- Chip YAML: [`spinor/registry/chips/ibm_heron_r2.yaml`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/registry/chips/ibm_heron_r2.yaml)
- Reference: [`ecr`](../reference/ecr.md), [`sx`](../reference/sx.md), [`rz`](../reference/rz.md)
