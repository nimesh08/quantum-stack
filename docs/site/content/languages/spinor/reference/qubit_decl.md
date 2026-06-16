# `qubit` — register declaration  *[declaration]*

Declares a fixed-size array of qubits. Every qubit starts in `|0⟩`.

## Synopsis

```
qubit <name> [ <size> ]
```

## Parameters

| name | constraint |
|---|---|
| name | identifier; unique within the file |
| size | positive integer literal; no expressions |

## Semantics

After this line, `name[0]`, `name[1]`, …, `name[size-1]` are
[qubit operands](../types.md#qubit) usable in any gate or
measurement.

The compiler tracks each slot SSA-style. Under the hardware contract,
the placement pass picks **physical** qubits for each slot.

## Legality

- W1 — every gate operand must reference a previously declared register.
- W2 — `q[k]` requires `0 <= k < size` (verify-time error).
- A second `qubit q[N]` declaration of the same name is a parse error.

## Examples

```spinor
target generic
qubit q[2]                  ; q[0], q[1] now exist, both in |0⟩
qubit aux[1]                ; another register, also fine
h q[0]
```

```spinor
qubit q[3]
h q[5]                      ; ERROR W2: index 5 out of range for q[3]
```

```spinor
qubit q[2]
qubit q[3]                  ; ERROR: redeclaration of 'q'
```

## Equivalents

- **Phonon**: identical.
- **Photon**: `QReg q(N)` — see [QReg](../../photon/reference/QReg.md).

## See also

- [`bit`](bit_decl.md) — classical bit register
- [`reset`](reset.md) — return a qubit to `|0⟩`
- [Types](../types.md)
