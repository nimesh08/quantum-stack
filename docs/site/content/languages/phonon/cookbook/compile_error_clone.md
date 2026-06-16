# Compile error: cloning a qubit

## What it does

Demonstrates the linear-type checker rejecting `r[0] = q[0]`.

## Recipe (broken)

```phonon
target generic
qubit q[2]
qubit r[1]
r[0] = q[0]                  ; ERROR
```

## What you'll see

```
error: cannot duplicate qubit value 'q[0]' (assigned to 'r[0]')
note: qubits are linear; the no-cloning theorem forbids copies
help: you probably want a swap or a separate Bell pair; see
      docs/site/languages/phonon/cookbook/teleport.md
   --> demo.phn:4:1
    |
  4 | r[0] = q[0]
    | ^^^^^^^^^^^
```

## How to fix it

You probably wanted **entanglement**, not a copy. Build a Bell pair:

```phonon
target generic
qubit q[2]
qubit r[1]
h q[0]
cx q[0], r[0]                ; q[0] and r[0] now entangled — share state via measurement
```

…or **swap**:

```phonon
target generic
qubit q[2]
qubit r[1]
swap q[0], r[0]              ; q[0]'s state is now at r[0]; q[0] is back to whatever r[0] held
```

…or **teleport** (long-distance, asymmetric, see
[teleport](teleport.md)).

## Why this is a feature, not a bug

Catching this at compile time saves you from running an incorrect
program on real hardware. Quantum debugging is expensive — every
shot costs money. The compiler is your free debugger.

## See also

[Linear types](../linear_types.md), [linearity rule](../rules/linearity.md),
[teleport](teleport.md)
