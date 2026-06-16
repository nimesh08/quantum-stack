# W3 — distinct operands on multi-qubit gates

A multi-qubit gate (`cx`, `cz`, `swap`, `ecr`, `ms`, `rzz`) cannot
use the same qubit twice.

## Why

The no-cloning theorem of quantum mechanics. You cannot create a
duplicate of a qubit's state, so `cx q[0], q[0]` is physically
nonsensical. We surface that fundamental law as a syntax-checker
rule.

## Diagnostic

```
error: W3: gate 'cx' requires distinct operands; got 'q[0], q[0]'
```

## Examples

```spinor
qubit q[2]
cx q[0], q[0]                ; ERROR W3
swap q[1], q[1]              ; ERROR W3
```

```spinor
qubit q[2]
cx q[0], q[1]                ; OK
```

## Fix

Use distinct operands.

## Why this matters beyond syntax

Linear-type systems in higher languages (Phonon's
[no-cloning rule](../../phonon/linear_types.md), Rust's ownership
model) generalise this. W3 is the simplest case: the same fundamental
law that makes
quantum-only-debugging-by-copy impossible.

## See also

[Phonon linear types](../../phonon/linear_types.md),
[`cx`](../reference/cx.md)
