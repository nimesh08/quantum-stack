# Linear types — physics as a compile error

Phonon's type checker does one thing no classical type checker had
to: **enforce a law of nature**. Qubits cannot be copied. The
compiler catches that at your desk for free.

## The no-cloning theorem

Quantum mechanics forbids copying an unknown quantum state. There
is no such thing as a "qubit copier"; the math is wrong (the would-be
copying operation isn't unitary).

Most languages would let you write `q2 = q1` and discover the
problem at runtime, on real hardware, after spending money. Phonon
catches it at compile time.

## How Phonon enforces it

Every `qubit` value has **linear** semantics:

- It can be passed to a gate as an operand (consumed and returned at
  the same slot).
- It can be passed to a `def` as a `qubit` parameter (the called
  function consumes it; on return the slot is alive again).
- It **cannot** be assigned to another variable.
- It **cannot** be returned (only `int` and `bit` can be returned).
- It **cannot** be passed twice in the same call (`cx q[0], q[0]` is
  rejected — same as W3 in Spinor).

## Worked compile errors

### Cloning

```phonon
qubit q[2]
qubit r[1]
r[0] = q[0]
```

```
error: cannot duplicate qubit value 'q[0]' (assigned to 'r[0]')
note: qubits are linear; the no-cloning theorem forbids copies
help: you probably want a swap or a separate Bell pair; see
      docs/site/languages/phonon/cookbook/teleport.md
```

### Reuse after measurement

```phonon
qubit q[1]
bit  c[1]
c[0] = measure q[0]
h q[0]
```

```
error: gate 'h' on qubit 'q[0]' which was just measured
note: measurement consumes the qubit's quantum state
help: insert 'reset q[0]' if your chip supports mid-circuit measurement
```

### Same operand twice

```phonon
qubit q[2]
cx q[0], q[0]
```

```
error: gate 'cx' requires distinct operands; got 'q[0], q[0]'
note: this is W3 from Spinor — the no-cloning law surfaced as a
      syntax-checker rule
```

## What's allowed

| Operation | OK? |
|---|---|
| `h q[0]` (gate consumes + reproduces the slot) | yes |
| `cx q[0], q[1]` (two distinct slots) | yes |
| `f(q[0])` where `f(qubit)` takes one qubit | yes |
| `c[0] = measure q[0]` (measurement consumes the qubit; bit gets the result) | yes |
| `reset q[0]` after measure (chip permitting) | yes |
| `int n; n = 4` | yes (int is not linear) |
| `bit c[1]; c[0] = c[0]` | yes (bit assignment is fine) |

## Why "linear types"

The type-system terminology is borrowed from Wadler's "Linear types
can change the world!" (1990). Rust's ownership model is the
modern realisation. Linear means a value is used **exactly once** at
each branch of the control flow.

In Phonon, "exactly once" is interpreted relative to **gate
operations**: every `qubit` slot is consumed-and-returned by the gate
that uses it, so the slot remains alive through a sequence of gates.
Cloning (`r = q`) creates a second alias, breaking the invariant.

## What this catches that nothing else does

- Accidentally writing `q[0]` twice in a multi-qubit gate (W3).
- Accidentally measuring then continuing to use the qubit
  (W4 in Spinor; also caught here).
- Writing `q2 = q1` thinking you're declaring an alias.
- Returning a `qubit` from a `def` (you can only return `int` or `bit`).

## See also

- [W3 distinct operands](../spinor/rules/W3_distinct_operands.md) — the
  Spinor-side check
- [W4 no op after measure](../spinor/rules/W4_no_op_after_measure.md)
- Cookbook: [compile_error_clone](cookbook/compile_error_clone.md),
  [compile_error_post_measure](cookbook/compile_error_post_measure.md)
