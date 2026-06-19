# Chip specialisation — same Phonon, different chips

## What it does

Show that the same Phonon program produces dramatically different
Spinor depending on the target.

## Recipe

```phonon
target generic
def bell(qubit a, qubit b) {
    h a
    cx a, b
}

qubit q[2]
bell(q[0], q[1])
```

Compile for three chips:

```bash
spinorc compile -t generic           bell.phn
spinorc compile -t ibm_heron_r2      bell.phn
spinorc compile -t ionq_forte        bell.phn
```

## Output (sketch)

| Chip | Output gates (after lowering) |
|---|---|
| `generic` | `h q[0]; cx q[0], q[1]` (unchanged) |
| `ibm_heron_r2` | `sx; rz; sx; rz; ecr; sx; rz` (placement on qubits 17, 18) |
| `ionq_forte` | `gpi2(pi/2); ms; gpi2; gpi2(pi/2)` (any pair) |

## Why it works

The Phonon → Spinor lowering produces a generic Spinor program. The
Spinor compile pipeline then does target-specific work:

- Placement picks physical qubits (uses last night's calibration data).
- Routing inserts `swap`s if needed (only on chips with limited
  coupling).
- Decomposition rewrites `h` and `cx` into the chip's native gates
  (KAK + Euler ZYZ).

The Phonon optimizer ran **once**, before any of that.

## Variations

- **Add a 3rd chip** (`quantinuum_helios`) — same program; the
  output uses `u1q + rzz`.
- **Look at the resource estimate**: `spinorc check -t <chip>`
  prints the gate counts and cost per chip.

## Same in Photon

Same: `q.h(0); q.cx(0, 1)` compiles to wildly different things
depending on the target. The Photon kernel doesn't change.

## Side effects on cost

Bell on `ibm_heron_r2` @ 1000 shots: $0.330. Same Bell on
`ionq_forte`: $10.00. **Same program, 30× cost difference.** This is
why the cost-control seam exists.

## Where to look

- Reference: [`def`](../reference/def.md)
- Cookbook: [all_to_all_vs_heavy_hex](../../spinor/cookbook/all_to_all_vs_heavy_hex.md)
- Tutorial: [Add a chip](https://nimesh08.github.io/heisenberg-platform/sdks/python/cookbook/)
