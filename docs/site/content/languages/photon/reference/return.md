# `return` — return from a kernel  *[control flow]*

Exit a `kernel` (or `def`) with a value.

## Synopsis

```photon
return                       ; void return (def only)
return <expression>          ; int / bit-array return
```

## Semantics

- Inside a `kernel`, the return value is the kernel's result — what
  the platform stores in `Result.counts` (when packed via
  `q.measure_int()`).
- Inside a `def`, the return is what the call expression evaluates to.
- Returning a `QReg` is rejected by the linear-type checker.

## Examples

```photon
kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}

def parity(bit c[2]) -> int {
    int p
    p = 0
    if (c[0] == 1) { p = p + 1 }
    if (c[1] == 1) { p = p + 1 }
    return p
}
```

## See also

[`kernel`](kernel.md), [Phonon `return`](../../phonon/reference/return.md)
