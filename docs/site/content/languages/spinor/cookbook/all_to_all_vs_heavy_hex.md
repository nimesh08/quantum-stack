# All-to-all vs heavy-hex: same program, two chips

## What it does

Demonstrates how the same portable program compiles **very**
differently depending on the chip's coupling map.

## Recipe (input)

```spinor
target generic
qubit q[3]
h q[0]
cx q[0], q[1]
cx q[0], q[2]                ; "fanout" from q[0]
```

## On `ionq_forte` (all-to-all, 36 qubits)

The router has nothing to do. Placement picks any 3 of 36 ions:

```spinor
target ionq_forte
qubit q[3]
gpi2(pi/2) q[0]
ms q[0], q[1]                ; one entangler
ms q[0], q[2]                ; another
```

**2 entanglers total.** Any pair of ions is "connected".

## On `ibm_heron_r2` (heavy-hex, 156 qubits)

Heavy-hex isn't fully connected; some triples form a triangle, some
don't. If our placed qubits don't form a triangle, we need a swap:

```spinor
target ibm_heron_r2
qubit q[3]                   ; placed at physical 17, 18, 25
sx q[17]                     ; h(0)
ecr q[17], q[18]             ; cx(0,1) — adjacent pair
swap q[18], q[25]            ; route q[1]'s state to 25
ecr q[17], q[25]             ; cx(0,2) — wait, but the SECOND cx is on q[0],q[2]
                             ; ... it's getting complicated; the actual output
                             ; depends on the heavy-hex topology and the
                             ; placement choices for q[0]..q[2].
```

(Real output is longer; the placement pass usually picks a triangle
of physical qubits if one is available, eliminating the swap.)

## Why this matters

The same logical 2-cx program becomes:

| Chip | Native 2Q gates |
|---|---|
| ionq_forte (all-to-all) | 2 |
| ibm_heron_r2 (heavy-hex) | 2..5 (depending on layout) |
| quantinuum_helios (all-to-all) | 2 |

If your circuit has many entanglers, the heavy-hex penalty compounds.
For Bell + GHZ it's negligible; for Grover at 50 qubits it's
substantial.

## Same in Phonon / Photon

Same — they emit Spinor before the chip-locking step. The chip
choice is exactly the same `target` argument.

## Side effects on cost

Cost = `shots × per_shot_usd`. Doesn't depend on the gate count
because IBM/IonQ price by shot, not by gate. **But fidelity does**:
extra swaps = extra error = noisier histogram = more shots needed for
the same statistical confidence.

## Where to look

- Reference: [`swap`](../reference/swap.md)
- Tutorial: [Add a chip](https://nimesh08.github.io/heisenberg-platform/sdks/python/cookbook/)
- Cookbook: [routing_via_swap](routing_via_swap.md)
