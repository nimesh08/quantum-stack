# `q.measure(...)`, `q.measure_int()`, `q.reset(i)` — measurement and housekeeping  *[QReg method]*

The three measurement-side methods.

## `q.measure(i)` — single qubit

Returns a `bit[1]` register.

```photon
QReg q(2)
q.h(0)
bit m = q.measure(0)
```

## `q.measure(start, end)` — range

(Where supported by your frontend.) Returns a `bit[N]` register
holding the measurement of `q[start..end]`.

## `q.measure_int()` — joint measurement as int

The most common form. Measures **every** qubit and packs the result
into an `int` (bit 0 = q[0], bit 1 = q[1], …).

```photon
kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()    ; 0b00 or 0b11
}
```

## `q.reset(i)` — reset to |0⟩

Returns `q[i]` to `|0⟩`. Only on chips with `supports.reset: true`.

```photon
QReg q(1)
q.h(0)
bit m = q.measure(0)
q.reset(0)
q.h(0)                       ; legal again after reset
```

## Equivalents

- **Spinor**: `c = measure q`, `c[k] = measure q[k]`, `reset q[k]`.
- **Phonon**: same.

## See also

[Spinor `measure`](../../../spinor/reference/measure.md), [Spinor `reset`](../../../spinor/reference/reset.md)
