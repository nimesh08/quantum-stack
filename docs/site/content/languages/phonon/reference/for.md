# `for` — counted loop  *[control flow]*

Bounded iteration with constant bounds. Unrolled at compile time.

## Synopsis

```
for <ident> in <int_lo>..<int_hi> <block>
```

## Parameters

| name | constraint |
|---|---|
| ident | new identifier; bound only inside the block |
| int_lo / int_hi | integer literals; `lo` inclusive, `hi` exclusive |

## Semantics

The body runs for each integer `i` in `[lo, hi)`. Compile-time
**unrolled**: the optimizer sees a flat sequence of `(hi - lo)` copies
of the body.

## Legality

- Bounds must be **integer literals** (no `int` variable references).
- Body uses `i` as a normal `int`.
- The unroll happens before linear-type checking, so each iteration
  is independently checked.

## Examples

```phonon
qubit q[3]
h q[0]
for i in 0..2 {
    cx q[i], q[i+1]
}
```

Equivalent (post-unroll):

```phonon
qubit q[3]
h q[0]
cx q[0], q[1]
cx q[1], q[2]
```

## Equivalents

- **Spinor**: cannot express loops; each iteration must be written by hand.
- **Photon**: `for i in range(lo, hi): ...` (Python decorator) or
  `for (int i = lo; i < hi; ++i) { ... }` (C++ frontend).

## See also

[`while`](while.md), [`def`](def.md),
[Compile-time bounds](../rules/compile_time_bounds.md)
