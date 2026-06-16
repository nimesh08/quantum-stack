# `assign` (`=`) — variable assignment  *[statement]*

Bind an `int` or `angle` variable to the value of an expression.

## Synopsis

```
<ident> = <expression>
```

## Semantics

`int` and `angle` variables can be assigned and reassigned. Each
write replaces the old value. **Registers (`qubit`, `bit`) cannot
be assigned** — they're declared once and indexed throughout.

## Legality

- LHS must be a previously-introduced `int`/`angle` variable, or a
  fresh declaration matching the expression's type.
- Cloning a `qubit` value (`r[0] = q[0]`) is rejected by the linear
  type checker.

## Examples

```phonon
int n
n = 4

angle theta
theta = pi / 2
```

```phonon
qubit q[2]
qubit r[1]
r[0] = q[0]                  ; ERROR: linear-type violation
```

## Equivalents

- **Spinor**: not present (no variables).
- **Photon**: standard `=`.

## See also

[`def`](def.md), [Linear types](../linear_types.md),
[Cookbook: compile_error_clone](../cookbook/compile_error_clone.md)
