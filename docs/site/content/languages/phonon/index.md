# Phonon

The middle layer. Adds **control flow** and **linear types** on top
of Spinor, while keeping every Spinor program legal as-is.

> **Phonon = Spinor + (`if` / `for` / `while` / `def` / variables) + linear types**

This is the IR the optimizer works on. It is a public language:
every shipped optimisation (cancellation, rotation merging, ZX,
scheduling) operates on Phonon and you can read its output with
`spinorc dump`. The linear type system enforces no-cloning *at
compile time* — physics shows up as a type error.

## A complete program

```phonon
target generic

def repeat_h(qubit q, int n) {
  for i in 0..n {
    h q
  }
}

qubit q[1]
bit c[1]
repeat_h(q[0], 4)              ; four Hadamards = identity
c = measure q
```

Same backend as Spinor, but you have functions, loops, and
parameters.

## What is in this section

- [Install](install.md) — Phonon ships in the same `spinorc` binary
  as Spinor.
- [Tutorial](tutorial.md) — write a teleportation circuit using
  `def` and conditional measurement.
- [Overview](overview.md) — the seven new things Phonon adds.
- [Lexical structure](lexical.md), [Grammar (delta over
  Spinor)](grammar.md), [Types](types.md).
- [Linear types — physics as a compile error](linear_types.md).
- [Cookbook](cookbook/index.md).

## Reference (Phonon-only constructs)

| Category | Pages |
|---|---|
| Conditionals | [`if`](reference/if.md) |
| Loops | [`for`](reference/for.md) · [`while`](reference/while.md) |
| Functions | [`def`](reference/def.md) · [`return`](reference/return.md) |
| Statements | [`assign`](reference/assign.md) · [`block`](reference/block.md) |
| Expressions | [`condition`](reference/condition.md) |

Every Spinor gate, declaration, and measurement is identically valid
in Phonon — see [Spinor](../spinor/index.md).

## Rules

| Rule | Page |
|---|---|
| Qubit lives in exactly one place | [Linearity](rules/linearity.md) |
| `for` / `while` need constant bounds | [Compile-time bounds](rules/compile_time_bounds.md) |
| `if` after `measure` needs chip support | [Feedforward legalisation](rules/feedforward_legalisation.md) |
| What to do when feedforward isn't supported | [Post-selection](rules/post_selection.md) |

---

!!! quote "Credits"
    **Phonon** was designed and implemented by **Nimesh Cheedella**
    as the structured-IR layer of the Heisenberg quantum stack.
