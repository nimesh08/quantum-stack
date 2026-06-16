# `for` — counted loop  *[control flow]*

Bounded iteration with constant bounds. Unrolled at compile time.

## Synopsis

```photon
for <ident> in <int_lo>..<int_hi> <block>
```

## Examples

```photon
QReg q(4)
q.h(0)
for i in 0..3 {              ; i = 0, 1, 2
    q.cx(i, i+1)
}
```

```photon
QReg q(2)
q.h(0)
for k in 0..5 {              ; 5 layers of phase shifts
    q.rz(pi/8, 0)
}
```

## Equivalents

- **Phonon**: identical syntax.
- **Python frontend**: `for i in range(0, 3)` — same semantics.
- **C++ frontend**: `for (int i = 0; i < 3; ++i)` — same.

## See also

[Phonon `for`](../../phonon/reference/for.md), [`if`](if.md)
