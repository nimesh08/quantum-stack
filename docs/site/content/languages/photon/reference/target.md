# `target` — Photon target declaration  *[top-level]*

Optionally declares the chip the kernel will be compiled for.

## Synopsis

```photon
target <chip_id>            ; e.g. target generic, target ibm_heron_r2
```

## Semantics

If present, this overrides the platform's default. If absent, the
target is supplied at compile time (e.g. `photonc-cxx build.yaml`'s
`target:` field, or `bell.compiled = compile_phonon(text, "ibm_heron_r2")`).

This is **the only top-level declaration** other than `kernel` and
`def`.

## Example

```photon
target generic               ; portable

kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}
```

## See also

[Spinor `target`](../../spinor/reference/target.md), [`kernel`](kernel.md)
