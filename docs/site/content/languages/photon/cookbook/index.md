# Photon — cookbook

Bite-sized, runnable recipes that exercise Photon's OO surface and
the `photon.lib` standard library.

## Programs

- [Bell, three doors](bell_three_doors.md) — the same program in
  `.pho`, Python, and C++; proof that all three frontends converge.
- [Grover with a custom oracle](grover_with_oracle.md) — pass an
  `Oracle` callback to `photon.lib.grover`.
- [QFT + phase estimation](qft_qpe.md) — `photon.lib.qft` /
  `iqft` plus the standard QPE recipe.
- [Teleport, end to end](teleport_full.md) — Bell pair, two
  measurements, classical-controlled Pauli corrections.
- [Variational loop](variational_loop.md) — `vqe_ansatz` plus an
  outer classical optimiser.

## Patterns

- [Using aliases](using_aliases.md) — `hadamard` vs `h`, `cnot` vs
  `cx`. Same gate, different name.
- [Compile to multiple targets](compile_to_target.md) — same kernel,
  several chips, in one script.

## Compile-error patterns

- [Error: too many qubits](error_too_many_qubits.md) — the kernel
  asks for more qubits than the chip has; how the compiler reports
  it.

---

!!! quote "Credits"
    **Photon** was designed and implemented by **Nimesh Cheedella**.
