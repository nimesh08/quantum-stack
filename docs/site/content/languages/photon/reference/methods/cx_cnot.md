# `q.cx(c, t)` and `q.cnot(c, t)` — Photon CNOT  *[QReg method]*

OO façade for [Spinor's `cx`](../../../spinor/reference/cx.md). Two
spellings, identical behaviour.

## Synopsis

```photon
q.cx(c, t)                   ; canonical
q.cnot(c, t)                 ; alias
```

## Parameters

| name | type | constraint |
|---|---|---|
| c | `int` | control index, `0 <= c < q.size()` |
| t | `int` | target index, distinct from `c` |

## Semantics

Flip `q[t]` iff `q[c]` is `|1⟩`. The standard entangler.

## Examples

```photon
QReg q(2)
q.h(0)
q.cx(0, 1)                   ; build a Bell pair
```

```photon
QReg q(2)
q.h(0)
q.cnot(0, 1)                 ; alias; identical compile output
```

```photon
QReg q(2)
q.cx(0, 0)                   ; ERROR: distinct operands required
```

## Equivalents

- **Spinor**: `cx q[0], q[1]`.
- **Phonon**: `cx q[0], q[1]`.

## See also

[Spinor `cx`](../../../spinor/reference/cx.md), [`cz`](cz.md), [`swap`](swap.md)
