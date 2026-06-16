# `reset` — return a qubit to `|0⟩`  *[housekeeping]*

Resets a qubit to the computational `|0⟩` state. Used to recycle
qubits after a measurement on chips that support it.

## Synopsis

```
reset <qubit>
```

## Semantics

Forces the qubit's classical state to `|0⟩`, regardless of where it
was. Discards any superposition or entanglement on that qubit.

## Legality

- W1, W2 — operand declared and in range.
- Only emitted on chips whose registry says
  `supports.reset: true` (most modern chips).

## Examples

```spinor
qubit q[1]
bit  c[1]
h q[0]
c[0] = measure q[0]
reset q[0]                   ; q[0] back to |0⟩
h q[0]                       ; legal again on chips with mid_circuit_measure
c[0] = measure q[0]
```

## Lowering

Hardware-supported on most modern chips; on others the legalisation
pass either rewrites the program or refuses with a precise error.

## Equivalents

- **Phonon**: identical.
- **Photon**: `q.reset(0)`.

## See also

[`measure`](measure.md), [W4](../rules/W4_no_op_after_measure.md)
