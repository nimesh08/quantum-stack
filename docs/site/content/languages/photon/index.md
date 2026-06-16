# Photon

The top layer. Object-oriented surface: quantum registers as objects,
gates as methods, a standard library of famous algorithms.

> **Photon = an OO façade over Phonon, plus three frontends (`.pho`,
> `@photon.kernel`, `[[photon::kernel]]`) that all converge on the same
> compiler engine.**

## On this page

- [Overview](overview.md) — three doors, one engine
- [Lexical structure](lexical.md)
- [Grammar](grammar.md)
- [Types](types.md) — `int`, `angle`, `bit`, `QReg`, `Oracle`

## Reference

### Top-level constructs

| | |
|---|---|
| [`kernel`](reference/kernel.md) | function marked as a quantum entry point |
| [`target`](reference/target.md) | optional target declaration |
| [`QReg`](reference/QReg.md) | the quantum register class |
| [`for`](reference/for.md) | counted loop |
| [`if`](reference/if.md) | conditional |
| [`return`](reference/return.md) | return from a kernel |

### `QReg` gate methods (every Spinor gate, with aliases)

| Group | Methods |
|---|---|
| One-qubit Cliffords | [`h`](reference/methods/h.md) · [`x`](reference/methods/x.md) · [`y`](reference/methods/y.md) · [`z`](reference/methods/z.md) · [`s`](reference/methods/s.md) · [`sdg`](reference/methods/sdg.md) |
| Non-Clifford | [`t`](reference/methods/t.md) · [`tdg`](reference/methods/tdg.md) |
| Continuous | [`rx`](reference/methods/rx.md) · [`ry`](reference/methods/ry.md) · [`rz`](reference/methods/rz.md) |
| Two-qubit | [`cx_cnot`](reference/methods/cx_cnot.md) · [`cz`](reference/methods/cz.md) · [`swap`](reference/methods/swap.md) |
| Native | [`sx_sxdg`](reference/methods/sx_sxdg.md) · [`ecr_ms_rzz`](reference/methods/ecr_ms_rzz.md) · [`gpi_gpi2_u1q`](reference/methods/gpi_gpi2_u1q.md) |
| Measurement | [`measure / measure_int / reset`](reference/methods/measure_measure_int_reset.md) |
| Aliases | [`hadamard`, `cnot`, `phase`](reference/methods/aliases.md) |

### `photon.lib` — the standard library

| | |
|---|---|
| [`bell_pair`](reference/library/bell_pair.md) | h + cx |
| [`ghz`](reference/library/ghz.md) | n-qubit GHZ |
| [`qft`](reference/library/qft.md) | Quantum Fourier Transform |
| [`iqft`](reference/library/iqft.md) | inverse QFT |
| [`grover`](reference/library/grover.md) | Grover search with an `Oracle` callback |
| [`teleport`](reference/library/teleport.md) | quantum teleportation |
| [`vqe_ansatz`](reference/library/vqe_ansatz.md) | layered Ry+CX ansatz |

### Frontends — three doors

| | |
|---|---|
| [`.pho` source](reference/frontends/pho_file.md) | the canonical text form |
| [`@photon.kernel`](reference/frontends/photon_kernel.md) | Python decorator |
| [`[[photon::kernel]]`](reference/frontends/cpp_attribute.md) | C++ attribute (Clang LibTooling or self-hosted) |

## Rules

| | |
|---|---|
| [Unsupported constructs](rules/unsupported_constructs.md) | what `@photon.kernel` rejects, and the precise error |
| [Three-door convergence](rules/three_door_convergence.md) | all three frontends produce the same Spinor |
| [Legalisation](rules/legalisation.md) | mid-circuit measure, feedforward, post-selection |

## Cookbook

- [Bell, three doors](cookbook/bell_three_doors.md) — same program in `.pho`, Python, C++
- [Grover with a custom oracle](cookbook/grover_with_oracle.md)
- [QFT + phase estimation](cookbook/qft_qpe.md)
- [Teleport, end to end](cookbook/teleport_full.md)
- [Variational loop](cookbook/variational_loop.md) — `vqe_ansatz` + outer optimizer
- [Using aliases](cookbook/using_aliases.md) — `hadamard` vs `h`, `cnot` vs `cx`
- [Compile to multiple targets](cookbook/compile_to_target.md)
- [Error: too many qubits](cookbook/error_too_many_qubits.md)
