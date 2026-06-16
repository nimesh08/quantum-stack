# Spinor — types

Spinor has exactly two value types and one parameter type. No
expressions, no arithmetic — just enough to express "this gate on
this qubit at this angle".

## `qubit`

A handle to a quantum value. Declared as a fixed-size array:

```spinor
qubit q[N]                  ; q[0] .. q[N-1] are now declared
```

Operations consume and produce qubit values (SSA-style under the
hood — every gate "rebinds" the slot). The compiler tracks each
qubit through the program.

| Property | Value |
|---|---|
| Sized at parse time | yes (literal integer) |
| Indexable | yes — `q[k]` |
| Linear | yes — see [W3 distinct operands](rules/W3_distinct_operands.md) for the syntactic check; full linear-type semantics live in [Phonon](../phonon/linear_types.md) |
| Copyable | **no** — physics forbids it (no-cloning) |

### Default state

Every freshly-declared qubit is in `|0⟩`. To explicitly reset for
re-use, see [`reset`](reference/reset.md).

## `bit`

A handle to a classical value. Same array shape:

```spinor
bit c[N]
```

Receives the result of a measurement. Bits **are** copyable — they're
classical data. Spinor doesn't expose arithmetic on bits; that's
Phonon.

### What you can do with a `bit` array

- **Be measured into**: `c = measure q` (whole array) or
  `c[k] = measure q[k]` (single).
- **Be returned** from a Photon kernel via [`q.measure_int()`](../photon/reference/methods/measure_measure_int_reset.md).

### What you can't do with a `bit` array

- Read `c[k]` to make a decision — for that you need
  [Phonon's `if`](../phonon/reference/if.md).
- Add, multiply, or compare. Spinor has no expressions.

## Indexing

Both `qubit` and `bit` arrays are zero-indexed:

```spinor
qubit q[3]
h q[0]              ; first qubit
cx q[0], q[2]       ; first and third
```

Out-of-range access is a [W2](rules/W2_index_in_range.md) error at verify
time:

```spinor
qubit q[2]
h q[5]              ; ERROR W2: index 5 out of range for q[2]
```

## Parameter types (gate angles only)

Continuous-rotation gates ([`rx`](reference/rx.md), [`ry`](reference/ry.md),
[`rz`](reference/rz.md), and the chip-native [`rzz`](reference/rzz.md))
take a single angle parameter. The grammar (`angle` rule):

```ebnf
angle ::= real | [ "-" ] "pi" [ "/" integer ] | real "*" "pi"
```

Examples:

```spinor
rx(pi)        q[0]      ; π = 180°
rx(pi/2)      q[0]      ; quarter turn
ry(0.7853)    q[0]      ; raw radians
rz(2*pi)      q[0]      ; full revolution (identity up to phase)
rx(-pi/4)     q[0]      ; unary minus
```

The lexer recognises `pi` as a token; the parser folds the expression
into a `double`. There is no general expression evaluator — the four
shapes above are the **complete** set Spinor accepts.

## What's missing (and where it lives)

Spinor is intentionally typeless beyond these two:

- Want **integers** for loop bounds? See [Phonon `int`](../phonon/types.md).
- Want **angles as parameters** that vary? See [Phonon `angle`](../phonon/types.md).
- Want **objects** with methods? See [Photon `QReg`](../photon/types.md).

## See also

- [Grammar](grammar.md) — exact production rules
- [W1 declare before use](rules/W1_declare_before_use.md)
- [W3 distinct operands](rules/W3_distinct_operands.md)
- [`qubit` declaration](reference/qubit_decl.md) and
  [`bit` declaration](reference/bit_decl.md)
