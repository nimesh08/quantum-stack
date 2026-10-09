# `while` — static or bounded device iteration

A static `while (condition) { ... }` is expanded at compile time. Its condition
must be a finite compile-time comparison and expansion must stay within the
operation budget. The compiler diagnoses an unbounded measurement-driven loop.

For a device condition, write an explicit positive bound:

```phonon
uint[8] count = 0
while (count < 3) max_iterations 4 {
    count = count + 1
}
output count
```

The compiler emits at most four guarded iterations and checks the predicate
after the last allowed body. `break` terminates normally; `continue` skips the
rest of an iteration. If the final predicate remains true, the result records
`loop_exhausted` for that shot. Every shot is retained. There is no automatic
postselection, retry, or host execution of measured conditions.

`bounded while (...) max_iterations N` is an accepted alias. Python kernels use
`while photon.bounded(condition, max_iterations=N):`. Targets must explicitly
support the required classical operations, branching and exhaustion outputs.

See the [finite controller language](../../../../../language/controller.md)
for nested-loop flags, return handling, limits and the C++ `boundedWhile` API.
