# Finite device-controller language

Controller values execute in the submitted program, once per shot. The compiler
never replaces a measured value with a host-side result or an assumed outcome.
Targets must advertise each required controller feature and supported integer
width. A legacy feed-forward capability alone does not authorize arithmetic.

## Values and expressions

Phonon declarations include `bool saved = c[0]`, `int saved = c[0]`, and
`uint[8] count = 0`. A saved measurement is an immutable snapshot: a later write
to `c[0]` cannot change it. The legacy `int` declaration keeps its signed 64-bit
compile-time meaning; its measured-bit snapshot contains only zero or one.
Runtime arithmetic requires an explicit unsigned width from 1 through 64.

Unsigned addition, subtraction and complement wrap modulo `2**width`. Bitwise
AND, OR and XOR preserve the width. Shifts initially require a compile-time
count in `[0,width)`. Comparisons are unsigned and yield Boolean values.
`uint[8](value)` explicitly truncates a wider value or zero-extends a narrower
one. Integer literals in unsigned declarations, assignments and casts are
parsed exactly, including `18446744073709551615` at width 64. Integers are never
serialized through a floating-point JSON number.

Phonon expression precedence, highest first, is unary operators, `* /`, `+ -`,
shifts, `&`, `^`, `|`, then comparisons. Runtime multiplication, division and
floating-point values remain outside the controller subset. Gate angles,
register sizes and qubit indices remain compile-time expressions. Use
parentheses when exchanging expressions with a frontend with different syntax.

Python kernels use `photon.uint(width, value)`, ordinary comparisons, `& | ^`,
`~` for unsigned complement and `not` for Boolean negation. `photon.output(x)`
exposes a named typed value. These helper functions are compiler source markers;
calling them as ordinary host functions raises an error.

## Control flow and bounded execution

Assignments to an existing controller variable in an `if`/`else` become typed
SSA joins. Both arms must preserve the variable's type and width. Branch-local
names do not escape their lexical scope. A join reads only the selected arm.

```text
uint[8] count = 0
while (count < 3) max_iterations 4 {
  count = count + 1
}
output count
```

`bounded while (...) max_iterations N` is retained as an alias. In Python:

```python
count = photon.uint(8, 0)
while photon.bounded(count < 3, max_iterations=4):
    count = count + 1
photon.output(count)
```

The positive bound is compile-time data. The compiler emits at most N guarded
device iterations, evaluating the predicate before each body and after the last
body. `break` exits without exhaustion; `continue` skips the rest of that body.
An exhaustion output is true only if execution remains live and the final
predicate is true. Skipped inner loops have a defined false exhaustion output.
Results retain every shot, including exhausted shots. The application records
an exhaustion status and CLI non-success exit code instead of silently truncating
the algorithm, filtering counts or retrying shots.

The Python kernel `run()` convenience method returns counts only on successful
execution. If a loop exhausts, or its completion could not be checked, it raises
`PhotonKernelError` with the saved job ID and the full execution result attached
as `error.result`. The persistent job and its counts retain every shot.

`QSTACK_EXPANDED_OPERATION_BUDGET` bounds emitted expansion (default 100000).
An oversized program fails compilation. Raising that limit never changes a
loop's explicit semantic bound.

The C++ Phonon Builder also exposes `boundedWhile(maxIterations, initialValues,
condition, body)`. Callbacks construct device IR over explicit loop-carried SSA
values; they cannot inspect a measurement on the host. The body returns
`LoopStep{values, optionalBreakPredicate}`. The result contains final values and
the exhaustion value. The unbounded legacy While marker is not an executable
runtime loop.

## Quantum lifetime and returns

New qubits inside a branch reserve static hardware capacity and start in
`|0>`. A branch-local name does not become visible after the branch. `discard q`
or `discard q[i]` consumes that source binding. The discarded state is reset,
and reusing its pool slot resets it before the next use. Using a discarded
binding is an error. Conditional disposal of an outer binding must obey
matching linear ownership at the join; unsupported ownership patterns receive
an explicit diagnostic.

Physical artifacts record `quantum_inputs` and `reserved_pool` as logical wire
indices. Full initial/final layouts still map those indices to device
addresses. Independent channel verification supplies arbitrary reference-
entangled input states only to `quantum_inputs`, initializes reserved wires to
zero and traces their final states out of the public quantum output.

Conditional quantum helper returns transfer returned states onto the fixed
caller wires. This supports permutations and fresh-wire replacement without
cloning a quantum state. Unconditional helper aliases retain their earlier
semantics. Phonon source helpers retain the quantum in/out parameter calling
convention; arbitrary expression-valued classical helper calls are a separate
language feature.

Python kernel returns preserve a fixed type and width across paths. Equal-width
measurements from different registers use a common return destination; Boolean
and unsigned returns use a common typed output. The public kernel count result
projects onto that declared return, excluding internal saved flags and storage.

The low-level Phonon Builder does not normalize arbitrary `If`/`Return` marker
combinations. Runtime conditional return markers are rejected explicitly; use
source return normalization or the bounded-loop callback API with explicit
carried values. General classical helper-call expressions remain outside this
release, even though Boolean/UInt kernel results are supported.
