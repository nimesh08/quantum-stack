# `while` — bounded loop  *[control flow]*

A loop that terminates at compile time. Use `for` instead for
counted iteration; `while` is here for completeness.

## Synopsis

```
while ( <condition> ) <block>
```

## Semantics

The compiler statically proves termination. Conditions involving
**only `int` literals and loop indices** can always be analysed; the
compiler unrolls. Conditions involving `bit[k]` from a runtime
`measure` are **rejected** unless they can be lowered into
post-selection.

## Legality

- The compiler must prove termination at compile time
  (see [Compile-time bounds](../rules/compile_time_bounds.md)).
- Body's bodies are checked for linearity per iteration.

## Example

```phonon
int i
i = 0
while (i < 4) {
    h q[i]
    i = i + 1
}
```

In practice prefer `for i in 0..4 { h q[i] }` — same result, simpler
to read.

## Equivalents

- **Spinor**: cannot express loops.
- **Photon**: deliberately omitted (use `for`).

## See also

[`for`](for.md)
