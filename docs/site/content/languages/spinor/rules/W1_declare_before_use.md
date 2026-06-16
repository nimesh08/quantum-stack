# W1 — declare before use

Every operand of a gate, measurement, or reset must reference a
register that was declared earlier in the file.

## Why

Type safety, basic. Catches typos and gives the verifier somewhere to
attach W2 (range) and W3 (distinct) checks.

## Diagnostic

```
error: W1: operand 'q[0]' references undeclared register 'q'
```

## Examples

```spinor
target generic
h q[0]                       ; ERROR W1: q not declared
```

```spinor
target generic
qubit q[2]
h r[0]                       ; ERROR W1: r not declared (typo)
```

## Fix

Declare the register before the first use:

```spinor
target generic
qubit q[2]
h q[0]                       ; OK
```

## See also

[`qubit`](../reference/qubit_decl.md), [`bit`](../reference/bit_decl.md),
[W2](W2_index_in_range.md)
