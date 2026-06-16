# `kernel` — entry point declaration  *[top-level]*

A function marked `kernel` is a quantum entry point — what the
platform actually runs.

## Synopsis

```photon
kernel <name> ( [ <param>* ] ) [ -> <return_type> ] <block>
```

## Parameters

Same as `def`:

| Type | Used for |
|---|---|
| `int` | constants, loop bounds (compile-time at call site) |
| `angle` | gate parameters |
| `bit` | classical input |
| `Oracle` | callback (see [Grover](library/grover.md)) |

`QReg` parameters are **not** allowed at kernel signature level — the
kernel constructs its own `QReg` internally. (This keeps every kernel
self-contained.)

## Semantics

- The platform finds a kernel by name (the `entry:` field in
  `photonc-cxx`'s YAML, or by importing the `@photon.kernel` decorator).
- Multiple kernels per file are allowed; each is compiled independently.
- A kernel **must** have a `return` of `int` (or `bit[N]`) — that's
  what the platform stores as the result.

## Examples

```photon
kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}

kernel grover_search(Oracle f) -> int {
    QReg q(4)
    q.grover(f, 3)
    return q.measure_int()
}
```

## Equivalents

- **Phonon**: top-level statements run after the last function;
  no special `kernel` keyword.
- **Spinor**: top-level only.
- **Python**: `@photon.kernel` decorator.
- **C++**: `[[photon::kernel]]` attribute.

## See also

[`def`](../../phonon/reference/def.md), [`QReg`](QReg.md),
[`@photon.kernel`](frontends/photon_kernel.md)
