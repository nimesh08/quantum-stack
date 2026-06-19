# Phonon — cookbook

Bite-sized, runnable recipes that exercise Phonon's structural
features — control flow, the linear type system, parameterised
sub-routines.

## Programs

- [Teleportation](teleport.md) — Bell pair + measure + classical-controlled X/Z.
- [Deutsch-Jozsa](deutsch_jozsa.md) — `def oracle` + balanced /
  constant flag.

## Patterns

- [Repeated routine](repeated_routine.md) — `def` + `for` + `reset`.
- [Classical branching](classical_branching.md) — `measure` → `int`
  → `if`.
- [Parameterised circuit](parameterized_circuit.md) — `angle`
  parameter to a `def`.
- [Chip specialisation](chip_specialization.md) — same Phonon
  source, different chips.

## Compile-error patterns

- [Compile error: clone](compile_error_clone.md) — `q2 = q1`
  rejected by the linear type checker.
- [Compile error: post-measure](compile_error_post_measure.md) — a
  gate after `measure` on a chip without `mid_circuit_measure`.

---

!!! quote "Credits"
    **Phonon** was designed and implemented by **Nimesh Cheedella**.
