# `def` — function declaration  *[function]*

Define a parameterised, inlinable subroutine.

## Synopsis

```
def <name> ( <param>* ) <block>
```

## Parameters

| Form | Meaning |
|---|---|
| `qubit q` | qubit operand (linear; consumed-and-returned by gates inside) |
| `bit c[N]` | bit register parameter |
| `int n` | integer parameter (compile-time constant or runtime) |
| `angle theta` | angle parameter (passed into rotation gates) |

## Semantics

Functions are **inlined** at every call site. There is no runtime
call. No recursion (direct or mutual). After inlining, the optimizer
sees a flat program.

## Legality

- All `qubit` parameters are passed by linear reference.
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

## Equivalents

- **Spinor**: cannot express functions.
- **Photon**: top-level `kernel` and library calls (`q.bell_pair(0, 1)`).

## See also

[`return`](return.md), [Cookbook: repeated_routine](../cookbook/repeated_routine.md)
