# `measure` — collapse a qubit into a bit  *[measurement]*

Reads a qubit (or whole register) into a classical bit (or whole
register). The measurement is in the computational basis. Probabilistic.

## Synopsis

```
<bit-or-array> = measure <qubit-or-array>
```

Two shapes:

```
c[k] = measure q[k]          ; single
c    = measure q             ; whole array (sizes must match)
```

## Semantics

After the measurement, the qubit is in the matching basis state
(`|0⟩` or `|1⟩`); the bit holds the classical result. Because
measurement is probabilistic, the program is run **shots** times and
the answer is the histogram.

## Legality

- W1, W2 — both operands declared.
- Whole-array form: `bit` and `qubit` register sizes must match.
- W4 — once a qubit has been measured, no further gate may act on it
  *unless* the chip's `supports.mid_circuit_measure` is true and a
  [`reset`](reset.md) was inserted.

## Examples

```spinor
qubit q[2]
bit  c[2]
h q[0]
cx q[0], q[1]
c = measure q                ; { "00": ~500, "11": ~500 } over 1000 shots
```

```spinor
qubit q[1]
bit  c[1]
c[0] = measure q[0]
h q[0]                       ; ERROR W4 on chips without mid_circuit_measure
```

## Lowering

Native on every chip — measurement is the readout primitive. The
emitter emits one `measure` per qubit-bit pair in the QASM3 output.

## Equivalents

- **Phonon**: identical, plus the result can drive `if` (feedforward).
- **Photon**: `q.measure()` returns a `bit[N]`; `q.measure_int()` packs
  the result as an `int`.

## See also

[`reset`](reset.md), [W4 no op after measure](../rules/W4_no_op_after_measure.md),
[Cookbook: Bell program](../cookbook/bell.md)
