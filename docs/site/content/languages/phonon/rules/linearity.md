# Quantum linearity and branch ownership

Quantum gates advance a wire's SSA value. The checker rejects stale values,
duplicate gate operands and duplicate returned aliases; one input cannot be
cloned into two independent quantum wires.

| Operation | Supported behavior |
|---|---|
| `h q[0]` | Returns the next value on the same wire |
| `cx q[0], q[1]` | Requires distinct wires |
| `cx q[0], q[0]` | Rejected |
| Helper quantum return | Distinct states transfer under the quantum in/out convention |
| Gate after measurement | Preserves the projected state, when target capabilities allow it |
| `discard q[0]` | Consumes the source binding and resets the pool slot |

Conditional helper returns preserve fixed caller wires. A branch-local fresh
qubit reserves static capacity. Conditional disposal of an outer live binding
with incompatible ownership at the join receives a diagnostic.

Measurement does not require an automatic reset: resetting would change the
quantum channel. See [wire ownership](../linear_types.md),
[measurement capability](../../spinor/rules/W4_no_op_after_measure.md), and the
[controller language](../../../../../language/controller.md).
