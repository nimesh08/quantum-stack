# `bit` — classical bit register  *[declaration]*

Declares a fixed-size array of classical bits. Receives the result of
[`measure`](measure.md).

## Synopsis

```
bit <name> [ <size> ]
```

## Parameters

| name | constraint |
|---|---|
| name | identifier; unique within the file |
| size | positive integer literal |

## Semantics

After this line, `name[0]`, `name[1]`, …, `name[size-1]` are
[bit operands](../types.md#bit). Bits start undefined; the only way
to set them is via `measure`.

Bits **are** copyable (classical data). Spinor doesn't expose
arithmetic on them — that's [Phonon](../../phonon/index.md).

## Legality

- W1 — every measurement target must reference a previously declared
  bit register.
- W2 — `c[k]` requires `0 <= k < size`.
- Re-declaring `c` is a parse error.

## Examples

```spinor
target generic
qubit q[2]
bit c[2]              ; c[0], c[1] declared
h q[0]
cx q[0], q[1]
c = measure q         ; both qubits read into both bits
```

```spinor
qubit q[2]
bit  c[1]
c = measure q         ; ERROR: register sizes don't match
```

## Equivalents

- **Phonon**: identical.
- **Photon**: implicit; `q.measure_int()` returns the joint
  measurement as an `int`.

## See also

- [`qubit`](qubit_decl.md)
- [`measure`](measure.md)
- [Types](../types.md)
