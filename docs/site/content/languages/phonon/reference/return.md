# `return` — return from a function  *[control flow]*

Exit a `def` with a value (or none).

## Synopsis

```
return                       ; void return
return <expression>          ; int / bit-array return
```

## Semantics

After inlining, `return` becomes a jump to the end of the inlined
body. Only `int`, `bit`, or `bit[N]` can be returned — `qubit` is
linear, cannot escape.

## Legality

- A `def` whose signature has an implicit return type may use bare
  `return`.
- Returning a `qubit` is an error (linear-type check).

## Example

```phonon
def parity(bit c[2]) -> int {
    int p
    p = 0
    if (c[0] == 1) { p = p + 1 }
    if (c[1] == 1) { p = p + 1 }
    return p
}
```

## Equivalents

- **Photon**: `return q.measure_int()` is the canonical kernel return.

## See also

[`def`](def.md)
