# `block` — `{ ... }`  *[syntax]*

A scope. Holds zero or more statements.

## Synopsis

```
{ <statement>* }
```

## Semantics

Blocks delimit `if`/`else`, `for`, `while`, and `def` bodies. Variables
declared inside a block are scoped to it. Newlines inside a block are
still significant — each statement still ends with a newline.

## Examples

```phonon
def f(qubit q) {
    h q
    if (true) {
        x q
    }
    z q                      ; back in the outer block
}
```

## Equivalents

- **Spinor**: doesn't have blocks (no nesting).
- **Photon**: same syntax (C-style braces).

## See also

[`def`](def.md), [`if`](if.md), [`for`](for.md)
