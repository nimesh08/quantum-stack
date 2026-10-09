# Compile-time and device loop bounds

`for` bounds and static `while` conditions are compile-time values. Signed
integer calculations preserve exact 64-bit values and diagnose overflow.
Register sizes, qubit indices and gate angles remain compile-time expressions.

Measurement-dependent iteration requires the explicit syntax
`while (predicate) max_iterations N { ... }`, where `N` is a positive
compile-time integer. The compiler emits a finite sequence of guarded device
operations, with a final exhaustion predicate. It never samples a condition on
the host to decide what program to compile.

`QSTACK_EXPANDED_OPERATION_BUDGET` limits emitted expansion (default 100000).
Changing this safety limit does not change a loop's semantic iteration bound.
An oversized expansion fails compilation.

All shots remain in results. Exhaustion is an application failure status, not a
reason to discard or replace shots. See [while](../reference/while.md) and the
[finite controller language](../../../../../language/controller.md).
