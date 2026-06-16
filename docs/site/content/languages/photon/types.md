# Photon — types

Five types: `int`, `angle`, `bit`, `QReg`, `Oracle`.

## `int`

Integer parameters and loop bounds. Standard arithmetic.

```photon
def repeat(QReg q, int rounds) {
    for i in 0..rounds {
        q.h(0)
    }
}
```

`int` parameters that appear in `for` bounds must be **compile-time
constants** at the call site (the unroll happens after inlining).

## `angle`

Gate parameter type for continuous rotations.

```photon
def parameterised(QReg q, angle theta) {
    q.rx(theta, 0)
    q.ry(theta * 2, 0)
    q.rz(theta + pi/2, 0)
}
```

Same shapes as Phonon's [angle](../phonon/types.md#angle).

## `bit`

A bit register — used for measurement results.

```photon
QReg q(2)
bit c[2]                     ; explicit bit array (rare; usually implicit)
```

Most kernels just use `q.measure_int()` which packs the result into
an `int` — no explicit `bit` declaration.

## `QReg(N)`

The quantum register. Constructor takes the qubit count. **Linear**:
cannot be assigned, returned, or duplicated. Same no-cloning rule as
Phonon — see [linear types](../phonon/linear_types.md).

```photon
QReg q(4)                    ; 4-qubit register, all in |0⟩
q.h(0)
q.cx(0, 1)
```

`QReg` exposes every Spinor gate as a method — see the
[QReg reference](reference/QReg.md).

## `Oracle`

A callback type for `lib.grover`. Declared as a parameter:

```photon
def my_oracle(QReg q) {
    q.cx(0, q.size() - 1)    ; mark |1...0⟩ — this is the "needle"
}

kernel search() -> int {
    QReg q(4)
    q.grover(my_oracle, 3)   ; 3 Grover rounds
    return q.measure_int()
}
```

The Oracle is inlined at every call. It's not a runtime callable.

## See also

- [QReg reference](reference/QReg.md)
- [Grover](reference/library/grover.md)
- [Linear types](../phonon/linear_types.md) (the underlying rule)
