# W2 — indices in range

Every `q[k]` access must satisfy `0 <= k < size`.

## Why

Catches off-by-one errors at compile time.

## Diagnostic

```
error: W2: index 5 out of range for 'q' [size 3]
```

## Examples

```spinor
qubit q[3]
h q[3]                       ; ERROR W2: 0..2 valid, 3 out of range
```

```spinor
qubit q[2]
cx q[0], q[2]                ; ERROR W2: q[2] out of range
```

## Fix

Adjust the index, or declare a bigger register:

```spinor
qubit q[3]
cx q[0], q[2]                ; OK
```

## See also

[W1](W1_declare_before_use.md), [`qubit`](../reference/qubit_decl.md)
