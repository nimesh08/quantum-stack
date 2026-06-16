# Compile-time bounds for `for` and `while`

Phonon loops must terminate at compile time. The unroll happens
before the optimizer runs.

## Why

The Phonon-to-Spinor lowering produces a flat gate list. There's no
runtime "loop" instruction in Spinor. So every iteration count must
be known at compile time.

For `for i in lo..hi { ... }`, the bounds are integer literals — easy
to unroll.

For `while (cond) { ... }`, the compiler has to prove termination by
analysing the condition + the loop body. Conditions involving only
`int` literals + loop indices can always be analysed; conditions that
depend on a runtime `measure` are rejected.

## Diagnostic

```
error: while loop bounds cannot be proven; condition reads
       runtime measurement m_anc[0]
help: use a `for` loop with a constant bound, or read the
       measurement once into an `int` if the bound is small
```

## Fix

Rewrite as `for`:

```phonon
for i in 0..3 { h q[0] }     ; unrolls to 3 hadamards
```

## See also

[`for`](../reference/for.md), [`while`](../reference/while.md)
