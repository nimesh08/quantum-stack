# `barrier` — optimizer fence  *[directive]*

Tells the optimizer "do not reorder gates across this line". No
runtime effect; purely a compile-time hint.

## Synopsis

```
barrier
barrier <qubit> { , <qubit> }
```

## Semantics

Acts as a synchronisation point. Bare `barrier` blocks **all** qubits;
operands constrain it to a subset.

Use cases:

- Force the optimizer to keep two clearly-separated sub-circuits
  apart (e.g. so cancellation passes don't merge them across).
- Mark the boundary between an oracle and the diffusion operator in
  Grover.
- Time-align operations on a chip with a notion of timing.

## Legality

- W1, W2 — any listed operands must be declared and in range.
- Always legal under both contracts.

## Examples

```spinor
qubit q[3]
h q[0]
cx q[0], q[1]
barrier                      ; protect this Bell pair
h q[2]                       ; the rest of the program is logically separate
```

```spinor
qubit q[3]
h q[0]
barrier q[0]                 ; only block reordering on q[0]
x q[1]                       ; free to reorder
```

## Lowering

Lowered to a `barrier` in the emitter output (QASM3 has the same
keyword); ignored at execution time.

## Equivalents

- **Phonon**: identical.
- **Photon**: not currently exposed (file an issue if you want it).

## See also

[Cookbook: barrier for the optimizer](../cookbook/barrier_for_optimizer.md)
