# `QReg` — quantum register class  *[type / class]*

The OO façade for a `qubit q[N]` declaration. Methods on `QReg`
correspond 1:1 to Spinor gate operands.

## Constructor

```photon
QReg q(N)                    ; N is a literal positive integer
```

Allocates an N-qubit register, all in `|0⟩`. Linear — cannot be
copied, returned, or aliased.

## Method categories

See the dedicated pages:

- One-qubit Cliffords: [`h`](methods/h.md), [`x`](methods/x.md),
  [`y`](methods/y.md), [`z`](methods/z.md), [`s`](methods/s.md),
  [`sdg`](methods/sdg.md).
- Non-Clifford: [`t`](methods/t.md), [`tdg`](methods/tdg.md).
- Continuous: [`rx`](methods/rx.md), [`ry`](methods/ry.md), [`rz`](methods/rz.md).
- Two-qubit: [`cx_cnot`](methods/cx_cnot.md), [`cz`](methods/cz.md), [`swap`](methods/swap.md).
- Native: [`sx_sxdg`](methods/sx_sxdg.md), [`ecr_ms_rzz`](methods/ecr_ms_rzz.md), [`gpi_gpi2_u1q`](methods/gpi_gpi2_u1q.md).
- Aliases: [`hadamard`, `cnot`, `phase`](methods/aliases.md).
- Measurement / housekeeping: [`measure / measure_int / reset`](methods/measure_measure_int_reset.md).
- Library: see [`photon.lib` reference](library/bell_pair.md) and siblings.

## Properties

- `q.size()` — returns the qubit count as `int` (compile-time
  constant, baked in at construction).

## Linearity

`QReg` is linear:

```photon
QReg q(2)
QReg r = q                   ; ERROR: cannot duplicate
```

Pass `QReg` to `def`s by reference (it's the same `QReg` value the
caller has):

```photon
def bell_pair(QReg q, int a, int b) {
    q.h(a)
    q.cx(a, b)
}

kernel main() -> int {
    QReg q(2)
    bell_pair(q, 0, 1)
    return q.measure_int()
}
```

## See also

[Types](../types.md), [Linear types](../../phonon/linear_types.md)
