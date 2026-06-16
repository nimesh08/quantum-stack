# Routing via `swap`

## What it does

When a `cx q[0], q[5]` is needed but the chip only connects neighbours,
the router inserts `swap`s to walk one of the qubits to the other.

## Recipe (input → output)

You write:

```spinor
target generic
qubit q[6]
h q[0]
cx q[0], q[5]                ; q[0] and q[5] are far apart
```

The compiler produces (sketch, on heavy-hex):

```spinor
target ibm_heron_r2
qubit q[6]
; ... place q[0]..q[5] on a chain ...
swap q[0], q[1]              ; walk q[0]'s state to position 1
swap q[1], q[2]              ; ... to 2
swap q[2], q[3]              ; ... to 3
swap q[3], q[4]              ; ... to 4 (now next to q[5])
ecr q[4], q[5]               ; the cx happens here, on a connected pair
```

Each `swap` costs **3 native two-qubit gates** of error.

## Why it works

SABRE is the routing algorithm:

1. Walk the program forward.
2. At each two-qubit gate whose operands aren't adjacent, score every
   possible `swap` by how much it reduces the distance to *this* gate
   plus a look-ahead window of future gates.
3. Apply the best swap.
4. Bidirectional pass: run the algorithm on the reversed program too.

For 156-qubit programs, good routing is the difference between
"usable result" and "noise".

## Variations

- **All-to-all chips** (IonQ): `swap` is never needed; the router is a
  no-op.
- **Heavy-hex** (IBM): some pairs are 4 hops apart in the worst case.
- **`barrier`** to fence routing decisions to a sub-circuit.

## Same in Phonon

Phonon doesn't know about routing — it produces portable Spinor; the
Spinor compile pipeline handles routing.

## Same in Photon

Same.

## Side effects on cost

If your program needs N swaps, the compiled program has 3N extra
native two-qubit gates. The resource estimator catches this — check
the `two_qubit_count` of the compiled output:

```bash
spinorc check -t ibm_heron_r2 your.spn
```

## Where to look

- Reference: [`swap`](../reference/swap.md), [`cx`](../reference/cx.md)
- Engineering deep-dive: SABRE algorithm in
  [`docs/build/phaseA/M5_placement_routing.md`](https://github.com/nimesh08/quantum-stack/blob/main/docs/build/phaseA/M5_placement_routing.md)
