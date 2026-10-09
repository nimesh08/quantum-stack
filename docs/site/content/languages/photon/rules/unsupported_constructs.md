# Python kernel language boundary

The Python frontend compiles a finite source subset. It never executes arbitrary
Python code against a measured device value.

Supported constructs include QReg methods, compile-time `range` loops, typed
Boolean/UInt expressions, measurement-controlled branches, explicit bounded
while loops, break/continue, scoped fresh registers and discard, and compatible
conditional kernel returns. See the
[finite controller language](../../../../../language/controller.md).

The following remain unsupported and receive diagnostics:

- Unbounded runtime loops, recursion and arbitrary helper-call expressions.
- Runtime multiplication, division, floating-point arithmetic, variable shifts
  and dynamic qubit indexing.
- Imports inside kernels, exceptions, context managers, I/O and assertions.
- Nested functions/classes, lambdas, generators, async constructs and runtime
  lists/dictionaries/sets.
- Implicit UInt-to-bool conversion, mismatched return widths/types, or paths
  that omit a required value return.

Use `while photon.bounded(condition, max_iterations=N):` for a runtime loop.
Use explicit comparisons for numeric conditions and `photon.uint(W, value)`
when a controller arithmetic width is required.

Unsupported syntax raises `UnsupportedConstructError`; target-specific
unsupported capabilities may be diagnosed later during physical compilation.
