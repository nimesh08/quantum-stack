# `condition` — boolean expression  *[expression]*

The thing inside the parens of `if` and `while`. A binary comparison.

## Synopsis

```
<expression> <op> <expression>
```

with `<op>` in `==`, `!=`, `<`, `<=`, `>`, `>=`.

## Semantics

Comparisons yield a `bit`-typed value at runtime (or constant-folded at
compile time when both sides are constant).

## Examples

```phonon
if (m_anc[0] == 1) { x dst }     ; runtime: feedforward needed
if (n == 0) { return }           ; compile-time int comparison
if (i < rounds) { h q[0] }       ; loop-style condition
```

## Equivalents

- **Photon**: `if cond == k:` (Python) or `if (cond) {}` (C++).

## See also

[`if`](if.md), [`while`](while.md), [Feedforward](../rules/feedforward_legalisation.md)
