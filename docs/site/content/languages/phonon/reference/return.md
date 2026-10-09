# `return` — typed helper results

Phonon helpers without an explicit result signature use the existing quantum
input/output calling convention. A helper
returns one distinct live qubit per qubit parameter; omitting a return keeps the
latest state of those parameters. A helper with no quantum parameters may return
no values. Angle parameters remain compile-time values; Boolean and UInt
parameters can carry device-side values.

```phonon
def choose(qubit a, qubit b, bit flag) {
    if (flag == 1) {
        return b, a
    }
    x a
    return a, b
}
```

Conditional returns suppress the remaining helper body and transfer the selected
states onto fixed caller wires. Permutations and fresh-wire replacements never
clone a state. Unconditional aliases preserve the existing helper convention.
All paths must preserve the number and quantum type of returned values.

An explicit result signature also supports classical values:

```phonon
def adjust(bool flag, uint[8] value) -> uint[8] {
    if (flag) {
        return value + 1
    }
    return value - 1
}
```

All reachable paths must return the declared types and arity. Tuple signatures
use `-> (uint[8], bool)`; multiple classical results are assigned to predeclared
names, for example `count, done = helper(...)`. Quantum results remain uniquely
owned and transfer onto fixed caller wires. Returning a classical value never
remeasures the qubit that produced it.

The C++ Builder normalizes conditional `returnOp` markers to guarded execution.
Operations after a return do not execute on that path. Its bounded-loop API
supports `breakLoop(values)` and `continueLoop(values)` at the transfer point,
as well as `returnOp` for exiting the enclosing function. A loop exit without
returning still requires a compatible function result.

See [def](def.md) and the
[finite controller language](../../../../../language/controller.md).
