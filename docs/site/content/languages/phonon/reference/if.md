# `if` — conditional execution  *[control flow]*

Run a block iff a classical condition holds. Optional `else` branch.

## Synopsis

```
if ( <condition> ) <block>
if ( <condition> ) <block> else <block>
```

## Parameters

| name | constraint |
|---|---|
| condition | comparison expression yielding `bit`-typed result |
| block | `{ statement* }` |

## Semantics

Three flavours, by what's in the condition:

| Condition references | When evaluated | Use |
|---|---|---|
| only `int` literals | compile time | dead-code elimination |
| only loop indices | compile time after loop unrolling | per-iteration unroll |
| a `bit[k]` from `measure` | **runtime** (feedforward) | classical-controlled gates |

The third flavour requires `chip.supports.feedforward` (or post-selection
fallback; see [feedforward_legalisation](../rules/feedforward_legalisation.md)).

## Legality

- W1, W2 on operands referenced in the body.
- The condition's type must be `bit` after evaluation (or
  `int == int` etc.).
- If the condition reads a `bit[k]` from a `measure`, the chip must
  support feedforward, OR the legalisation pass falls back to
  post-selection.

## Examples

```phonon
def teleport(qubit src, qubit anc, qubit dst) {
    h anc
    cx anc, dst
    cx src, anc
    h src
    bit m0[1]
    bit m1[1]
    m0 = measure src
    m1 = measure anc
    if (m1[0] == 1) { x dst }
    if (m0[0] == 1) { z dst }
}
```

```phonon
; compile-time conditional, used in the cookbook to demonstrate
; conditional code generation:
def maybe_h(qubit qq, int flag) {
    if (flag == 1) { h qq } else { x qq }
}
```

## Equivalents

- **Spinor**: cannot express conditionals.
- **Photon**: `if (cond) { ... } else { ... }`.

## See also

[`condition`](condition.md), [`measure`](../../spinor/reference/measure.md),
[Cookbook: teleport](../cookbook/teleport.md),
[Feedforward legalisation](../rules/feedforward_legalisation.md)
