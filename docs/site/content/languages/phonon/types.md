# Phonon — types

Four types: `qubit`, `bit`, `int`, `angle`. Two are linear (qubit) /
register-only (bit), two are ordinary copyable values.

## `qubit`

Same as in Spinor — a handle to a quantum value, declared as a
fixed-size array. **Linear**: cannot be assigned, returned, or
duplicated. See [Linear types](linear_types.md) for the full story.

```phonon
qubit q[2]
qubit r[1] = q[0]            ; ERROR: cannot duplicate a qubit
```

## `bit`

Same as in Spinor — a classical bit register, written by `measure`.
Bits are copyable in principle but Phonon doesn't (yet) expose bit
arithmetic; you can only read `c[k]` inside a `condition`.

```phonon
bit c[2]
c = measure q
if (c[0] == 1) { x q[1] }
```

## `int`

Loop variables and integer parameters. Plain arithmetic.

```phonon
def repeat(qubit qq, int rounds) {
    for i in 0..rounds {
        h qq
    }
}
```

`int` values can be assigned, passed, returned, used in expressions.
The parser folds `int` expressions to a constant where possible
(constant folding pass).

## `angle`

Gate parameter type. Same shapes as the literal angle in Spinor (`pi`,
`pi/N`, `real`, `real*pi`), plus identifier references and integer-style
expressions:

```phonon
def parameterised(qubit qq, angle theta) {
    rx(theta)         qq
    ry(theta * 2)     qq      ; OK — multiplied by integer literal
    rz(theta + pi/2)  qq      ; OK — sum with another angle literal
}
```

## Cross-reference

| Type | Spinor | Phonon | Photon |
|---|---|---|---|
| `qubit` | yes | yes | as `QReg` (object wrapper) |
| `bit` | yes (register) | yes (register) | implicit in `measure`/`measure_int` |
| `int` | — | yes | yes (return type, parameters) |
| `angle` | inline literal only | yes (parameter) | yes (parameter) |

## See also

- [Linear types](linear_types.md)
- [Spinor types](../spinor/types.md) — the inherited base
- [Reference: `def`](reference/def.md)
