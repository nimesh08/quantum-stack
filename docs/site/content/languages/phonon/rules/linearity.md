# Linearity — qubit lives in exactly one place

The headline rule: a `qubit` value cannot be aliased, duplicated, or
returned. See the [Linear types primer](../linear_types.md) for the
full picture.

## What's checked

| Operation | OK? | Why |
|---|---|---|
| `h q[0]` | yes | gate consumes-and-returns the slot |
| `cx q[0], q[1]` | yes | two distinct slots |
| `r[0] = q[0]` | **NO** | clones |
| return a `qubit` from a `def` | **NO** | escapes the linear scope |
| `cx q[0], q[0]` | **NO** | W3 — same slot twice |
| `h q[0]` after `measure q[0]` | **NO** (without reset) | W4 — qubit consumed |

## Diagnostic

```
error: cannot duplicate qubit value 'q[0]' (assigned to 'r[0]')
note: qubits are linear; the no-cloning theorem forbids copies
```

## Fix

Most "I want two of the same qubit" code is actually trying to share
state via entanglement. Swap your `r = q` for a Bell pair or a swap.

## See also

[Linear types](../linear_types.md), [W3](../../spinor/rules/W3_distinct_operands.md),
[W4](../../spinor/rules/W4_no_op_after_measure.md)
