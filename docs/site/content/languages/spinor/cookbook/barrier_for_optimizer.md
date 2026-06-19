# Use `barrier` to fence the optimizer

## What it does

Stops the Phonon optimizer from reordering gates across a logical
boundary. No runtime cost; pure compile-time directive.

## Recipe

```spinor
target generic
qubit q[2]
h q[0]
cx q[0], q[1]
barrier                      ; Bell pair done; do not reorder past here
h q[0]                       ; this h is logically separate
cx q[0], q[1]
```

Without the barrier, the optimizer might cancel the second `h` against
the first if the cancellation pass deems them equivalent.

## Why it works

The Phonon optimizer's commutation pass walks the gate list looking
for cancellations and reorderings. `barrier` is a phonon op that the
pass refuses to commute past. The barrier is preserved in the QASM3
output so providers honour it too.

## Variations

- **Per-qubit barrier**: `barrier q[0]` blocks reorder only on q[0];
  q[1] is free.
- **Multi-qubit barrier**: `barrier q[0], q[2]`.

## Same in Phonon

Identical. Especially useful with `for` loops where you want to
preserve iteration boundaries.

## Same in Photon

Not currently exposed (`q.barrier()` is on the roadmap).

## Side effects on cost

Zero — `barrier` is not a gate.

## Where to look

- Reference: [`barrier`](../reference/barrier.md)
- Phonon optimizer overview: [Operations guide](../../../operations/index.md)
