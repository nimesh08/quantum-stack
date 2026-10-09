# `return` — quantum helper results

Phonon source helpers use a quantum input/output calling convention. A helper
returns one distinct live qubit per qubit parameter; omitting a return keeps the
latest state of those parameters. A helper with no quantum parameters may return
no values. Numeric helper parameters are compile-time values.

```phonon
def choose(qubit a, qubit b, bit flag) {
    if (flag == 1) {
        return b, a
    }
    x a
    return a, b
}
```

Conditional returns suppress the remaining helper body and transfer the selected
states onto fixed caller wires. Permutations and fresh-wire replacements never
clone a state. Unconditional aliases preserve the existing helper convention.
All paths must preserve the number and quantum type of returned values.

General expression-valued classical helper calls are not implemented. Use named
controller values and `output value` in Phonon, or fixed-type Boolean/UInt kernel
returns in the Photon and Python frontends.

The low-level C++ Builder does not normalize a raw `If` containing `Return`.
Such runtime markers receive an explicit diagnostic; source frontends perform
return normalization before lowering. The Builder's bounded-loop API carries
explicit typed values through its callback result.

See [def](def.md) and the
[finite controller language](../../../../../language/controller.md).
