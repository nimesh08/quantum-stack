# W4 — no gate on a measured qubit (without reset)

Once you `measure` a qubit, you cannot apply further gates to it
**unless** the chip supports mid-circuit measurement *and* you insert
a [`reset`](../reference/reset.md) before the next gate.

## Why

Measurement collapses the qubit into a classical state. On many
chips the readout pulse damages the qubit further so it's not even
in a clean `|0⟩`/`|1⟩` afterwards. The legalisation pass needs the
explicit `reset` to know it's safe.

## Diagnostic

```
error: W4: gate 'h' on measured qubit 'q[0]'; insert 'reset' or
       compile for a chip with supports.mid_circuit_measure
```

## Examples

```spinor
qubit q[1]
bit  c[1]
h q[0]
c[0] = measure q[0]
h q[0]                       ; ERROR W4
```

```spinor
target ibm_heron_r2          ; supports.mid_circuit_measure: true
qubit q[1]
bit  c[1]
h q[0]
c[0] = measure q[0]
reset q[0]
h q[0]                       ; OK
```

## Fix

- Insert a `reset`, or
- Defer the measurement to the end of the program, or
- Switch to a chip that supports mid-circuit measurement.

## See also

[`measure`](../reference/measure.md), [`reset`](../reference/reset.md),
[Cookbook: reset and reuse](../cookbook/reset_and_reuse.md)
