# `if` — conditional execution  *[control flow]*

Run a block conditionally. Optional `else`.

## Synopsis

```photon
if ( <condition> ) <block>
if ( <condition> ) <block> else <block>
```

## Semantics

Same three flavours as Phonon's `if`:

| Condition | When evaluated |
|---|---|
| `int` literals only | compile time |
| loop indices | compile time after unroll |
| `bit[k]` from `measure` | runtime (feedforward; needs chip support) |

## Examples

```photon
QReg q(2)
bit  m[1]
q.h(0)
m = q.measure(0)
if (m[0] == 1) {
    q.x(1)                   ; classically-controlled X
}
```

## Equivalents

- **Phonon**: identical.
- **Python frontend**: `if cond == k:`.
- **C++ frontend**: `if (cond) { ... } else { ... }`.

## See also

[Phonon `if`](../../phonon/reference/if.md),
[Phonon feedforward](../../phonon/rules/feedforward_legalisation.md)
