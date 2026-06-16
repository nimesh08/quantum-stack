# Phonon

The middle layer. Adds **control flow** and **linear types** on top of
Spinor, while keeping every Spinor program legal as-is.

> **Phonon = Spinor + (`if` / `for` / `while` / `def` / variables) + linear types**

This is the IR the optimizer works on. It's a public language: every
shipped optimization (cancellation, rotation merging, ZX, scheduling)
operates on Phonon and you can read its output with `spinorc dump`.

## On this page

- [Overview](overview.md)
- [Lexical structure](lexical.md)
- [Grammar (delta over Spinor)](grammar.md)
- [Types](types.md)
- [Linear types — physics as a compile error](linear_types.md)

## Reference (Phonon-only constructs)

| Category | Pages |
|---|---|
| Conditionals | [`if`](reference/if.md) |
| Loops | [`for`](reference/for.md) · [`while`](reference/while.md) |
| Functions | [`def`](reference/def.md) · [`return`](reference/return.md) |
| Statements | [`assign`](reference/assign.md) · [`block`](reference/block.md) |
| Expressions | [`condition`](reference/condition.md) |

Every Spinor reference page (gates, declarations, measurements) is
identically valid in Phonon — see the [Spinor reference](../spinor/index.md#reference-every-keyword-has-its-own-page).

## Rules

| | Rule |
|---|---|
| [Linearity](rules/linearity.md) | qubit lives in exactly one place |
| [Compile-time bounds](rules/compile_time_bounds.md) | `for` and `while` need constant bounds |
| [Feedforward legalisation](rules/feedforward_legalisation.md) | `if` after `measure` needs chip support |
| [Post-selection](rules/post_selection.md) | what to do when feedforward isn't supported |

## Cookbook

- [Teleportation](cookbook/teleport.md) — Bell pair + measure + classical-controlled X/Z
- [Deutsch–Jozsa](cookbook/deutsch_jozsa.md) — `def oracle` + balanced/constant flag
- [Repeated routine](cookbook/repeated_routine.md) — `def` + `for` + `reset`
- [Classical branching](cookbook/classical_branching.md) — `measure` → `int` → `if`
- [Parameterised circuit](cookbook/parameterized_circuit.md) — `angle` parameter to a `def`
- [Compile error: clone](cookbook/compile_error_clone.md) — `q2 = q1` rejected
- [Compile error: post-measure](cookbook/compile_error_post_measure.md) — gate after `measure`
- [Chip specialisation](cookbook/chip_specialization.md) — same Phonon, different chips
