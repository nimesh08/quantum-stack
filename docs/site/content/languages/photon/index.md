# Photon

The top layer. Object-oriented surface: quantum registers as
objects, gates as methods, a standard library of famous algorithms.

> **Photon = an OO façade over Phonon, plus three frontends
> (`.pho`, `@photon.kernel`, `[[photon::kernel]]`) that all converge
> on the same compiler engine.**

If you write *application* code that includes a quantum kernel,
this is the language you write. The Python decorator and the C++
attribute let you embed Photon kernels inside an existing Python or
C++ program; the `.pho` file format is the canonical text form when
the kernel is a standalone unit.

## A complete program (Python door)

```python
from photon import kernel

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()
```

Run it:

```python
from photon import compile, run

artefact = compile(bell, target="ibm_heron_r2")
hist = run(artefact, shots=1000, mode="cassette")
print(hist)        # {"00": 503, "11": 497}
```

Same kernel, same result, written instead in a `.pho` file:

```photon
target ibm_heron_r2

kernel bell() -> bit[2] {
  let q = QReg(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure();
}
```

Three doors, one engine. The C++ attribute door is documented in
[`reference/frontends/cpp_attribute.md`](reference/frontends/cpp_attribute.md).

## What is in this section

- [Install](install.md) — `pip install heisenberg-photon` (Python),
  build `photonc-cxx` from source (C++).
- [Tutorial](tutorial.md) — a Bell program walked through all three
  frontends.
- [Overview](overview.md), [Lexical](lexical.md),
  [Grammar](grammar.md), [Types](types.md).
- [Cookbook](cookbook/index.md).

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

---

!!! quote "Credits"
    **Photon** was designed and implemented by **Nimesh Cheedella**
    as the user-facing language of the Heisenberg quantum stack.
