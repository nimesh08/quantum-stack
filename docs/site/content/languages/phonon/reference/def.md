# `def` — function declaration  *[function]*

Define a parameterised, inlinable subroutine.

## Synopsis

```
def <name> ( <param>* ) [ -> <result-type-or-tuple> ] <block>
```

## Parameters

| Form | Meaning |
|---|---|
| `qubit q` | qubit operand (linear; consumed-and-returned by gates inside) |
| `bit c` | one measured-bit slot parameter |
| `bool flag` | Boolean controller value |
| `uint[W] value` | unsigned controller value with explicit width 1–64 |
| `int n` | exact signed integer parameter (compile-time constant) |
| `angle theta` | angle parameter (passed into rotation gates) |

## Semantics

Functions are **inlined** at every call site. There is no runtime
call. No recursion (direct or mutual). After inlining, the optimizer
sees a flat program.

## Legality

- All `qubit` parameters are passed by linear reference.
- Runtime Boolean and UInt arguments retain their types and widths. Rotation
  angles and quantum indices must still resolve at compile time.
- An explicit return signature fixes the types and arity on every reachable
  path. A single classical result can be used in an expression; multiple
  classical results can be assigned to predeclared names.
- Without a return signature, the existing quantum convention returns one live
  qubit per quantum parameter, with distinct aliases. See [return](return.md)
  for conditional state transfer and mixed results.
- A function cannot be re-defined.
- Calling a function not yet declared is an error (forward declarations
  not supported).

## Examples

```phonon
def bell_pair(qubit a, qubit b) {
    h a
    cx a, b
}

qubit q[2]
bell_pair(q[0], q[1])
```

```phonon
def grover_step(qubit qq, angle theta) {
    rz(theta) qq
    h qq
}

qubit q[1]
for i in 0..3 {
    grover_step(q[0], pi/2)
}
```

```phonon
def choose(bool flag, uint[8] value) -> uint[8] {
    if (flag) {
        return value + 1
    }
    return value - 1
}

qubit q[1]
bit c[1]
c[0] = measure q[0]
uint[8] result = choose(c[0], uint[8](7))
output result
```

## Equivalents

- **Spinor**: cannot express functions.
- **Photon**: top-level `kernel` and library calls (`q.bell_pair(0, 1)`).

## See also

[`return`](return.md), [Cookbook: repeated_routine](../cookbook/repeated_routine.md)
