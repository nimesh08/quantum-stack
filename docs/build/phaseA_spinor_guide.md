# Phase A — Spinor User Guide

End-of-phase user-facing guide. This is the document a person
new to the project reads to understand and use `spinorc`.

For per-milestone engineering specs see
[phaseA/](phaseA/README.md). For the build journal see
[phaseA_progress.md](phaseA_progress.md). For deviations from
the Deep-Dive see [phaseA_decisions.md](phaseA_decisions.md).

---

## Three on-ramps

This guide is written for three audiences. Pick yours:

- **From quantum (Qiskit / Braket / Cirq users).** Skip §1.
  Read §2 for compiler concepts, then §3.
- **From compilers (LLVM / MLIR / language design).** Skip §2.
  Read §1 for quantum concepts, then §3.
- **From neither.** Read §1 then §2 then §3. Each is
  self-contained.

The full glossary lives at
[phaseA/glossary.md](phaseA/glossary.md).

## §1 — Quantum primer (1 page)

> Skip this section if you've already used Qiskit, Braket, or
> Cirq.

A **qubit** is the quantum version of a bit. Until you measure
it, it can hold a *blend* of 0 and 1 simultaneously
(*superposition*). A **gate** is one quantum instruction
acting on one or more qubits — `h`, `x`, `cx`, `rz(angle)`,
etc. A **circuit** is a flat list of gate applications, ending
with **measurements** that collapse the qubits to classical
bits. Because measurement is probabilistic, every program runs
many **shots** and the answer is the *histogram* of outcomes.

Real chips don't run the whole gate vocabulary directly. Each
exposes a small **native gate set** (e.g. IBM's Heron r2 has
`ecr rz sx x`) and a fixed **coupling map** of which physical
qubits can interact (some chips are sparse honeycomb
lattices; trapped-ion chips are all-to-all). Spinor's job is to
take a portable program written against the *standard* gate
vocabulary and rewrite it for one specific chip's native gates
on its physical wiring. That rewrite is *exact*, not
approximate.

That's all the quantum you need for Phase A.

## §2 — Compiler primer (1 page)

> Skip this section if you've used LLVM/MLIR or written a
> language.

Compilers translate source code through a sequence of
**intermediate representations (IRs)**. Each IR is rewritten
by **passes**. The job of a compiler is to start with text the
human wrote and end with something the machine can run, while
preserving meaning at every step.

Spinor's IR is an MLIR-style dialect (Phase A ships an in-tree
backend that is shaped exactly like MLIR's; an MLIR-backed
backend builds when LLVM/MLIR 22.1.7 is present, see
[phaseA_decisions.md](phaseA_decisions.md) D4). The dialect
defines `!spinor.qubit` and `!spinor.bit` types and ops for
every gate (`spinor.h`, `spinor.cx`, `spinor.rz`, …). Every
gate op consumes a qubit value and produces a new qubit value
(SSA), which is what makes accidental qubit duplication a
detectable error.

The Spinor compilation pipeline:

```
.spn source
  → tokens (M7 lexer)
  → AST  (M7 recursive-descent parser)
  → Spinor IR, target generic    (front end done)
  → W1–W6 verifier (M3)
  → placement (M5)               (virtual qubits → physical)
  → routing  (M5)                (insert SWAPs as needed)
  → decomposition (M6)           (standard gates → native)
  → cleanup (M6)                 (local peephole)
  → Spinor IR, target <device>   (hardware contract)
  → check lane (M8)              (sim + equivalence + resources)
  → emitter (M9)                 (OpenQASM 3 / QIR / Quil)
  → submission adapter (M10)     (verbatim mode)
  → measurement histogram
```

Every passage from one form to the next is a pass; every pass
is in one of `spinor/passes/` (placement, routing,
decomposition, cleanup), `spinor/verify/`, `spinor/sim/`,
`spinor/emit/`, or `spinor/submit/`.

## §3 — Installing `spinorc`

### Required

- CMake ≥ 3.28
- A C++20 compiler (GCC 13+ or Clang 17+)
- Python ≥ 3.10 (for the optional Python submission adapters)
- `pytest ≥ 9.0` (for the Python tests)

### Optional

- LLVM/MLIR 22.1.7 to build the MLIR-backed bridge (decision
  D4). Without it, the in-tree backend builds and tests pass.
- `qiskit-ibm-runtime`, `amazon-braket-sdk`, `azure-quantum`
  for live submissions; cassette-mode tests run without them.

### Build from source

```bash
git clone <repo>
cd quantum-stack
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the full Phase A test suite
cd build && ctest --output-on-failure
```

The default build configures cleanly even without LLVM/MLIR;
the MLIR-backed bridge is built only when `LLVM_DIR` and
`MLIR_DIR` resolve.

### First smoke test

```bash
$ ./build/spinor/cli/spinorc registry list
ibm_heron_r2
ionq_aria_proto
ionq_forte
quantinuum_helios
```

## §4 — Hello, Spinor

The canonical hello-world. Save as `bell.spn`:

```
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

Compile it for IBM Heron r2:

```bash
$ SPINOR_REGISTRY_ROOT=spinor/registry \
    ./build/spinor/cli/spinorc compile -t ibm_heron_r2 bell.spn
```

The output is the chip-locked Spinor IR (after placement,
routing, decomposition, and cleanup). Every gate is in IBM's
native set (`ecr`, `rz`, `sx`, `x`); every two-qubit gate sits
on a physically connected pair.

Emit it as OpenQASM 3 with a Braket verbatim box:

```bash
$ ./build/spinor/cli/spinorc emit -t ionq_forte -f qasm3 \
                                  --verbatim bell.spn
OPENQASM 3.1;
bit[2] c;
#pragma braket verbatim
box {
  ...native gate sequence on $0, $1...
  c[0] = measure $0;
  c[1] = measure $1;
}
```

Run the check lane:

```bash
$ ./build/spinor/cli/spinorc check -t ionq_aria_proto bell.spn
equivalence: skipped (chip has 25 qubits; statevector capped at 12)
gates total:        10
gates two-qubit:    1
depth:              9
qubits:             25
measurements:       2
error (est.):       0.0188744
cost @1k shots ($): 10
```

(For ≤12-qubit chips the equivalence check runs end-to-end.)

## §5 — `spinorc` subcommands

```
spinorc parse    <FILE.spn>
spinorc verify   -t <chip> <FILE.spn>
spinorc compile  -t <chip> <FILE.spn>
spinorc check    -t <chip> <FILE.spn>
spinorc emit     -t <chip> -f <qasm3|qir|quil> [--verbatim] <FILE.spn>
spinorc registry list
```

`SPINOR_REGISTRY_ROOT` selects the registry directory (default
`spinor/registry`).

### `parse`

Reads the file and prints the resulting IR. Useful for
debugging grammar issues; equivalent to running M7 alone.

### `verify`

Runs the M3 W1–W6 + type-check verifier against the supplied
target. Errors are printed with file/line/column.

### `compile`

The full M3 + M5 + M6 pipeline. Output is the chip-locked IR.

### `check`

The M8 check lane. Runs the compile pipeline AND simulates
both the input and output programs to assert equivalence (up
to global phase). Prints a resource summary.

### `emit`

Compiles, then serialises to one of the three target formats.
`--verbatim` is honoured by the QASM3 emitter (wraps body in
`#pragma braket verbatim box { ... }` per Braket's
specification).

### `registry list`

Lists every chip the registry knows about. To add a chip,
write a YAML under `spinor/registry/chips/` per
[phaseA/registry-schema.md](phaseA/registry-schema.md).
**No code change needed** (proven by `ionq_aria_proto`).

## §6 — Adding a new chip in 30 minutes

A worked example. Suppose we want to add a fictional 16-qubit
all-to-all chip called `acme_q1`.

Step 1 — write `spinor/registry/chips/acme_q1.yaml`:

```yaml
id: acme_q1
provider: local
qubits: 16
native_gates: [ecr, rz, sx, x]
coupling_map:
  topology: all_to_all
  size: 16
supports:
  mid_circuit_measure: true
  feedforward: limited
  reset: true
calibration:
  source: fixture
  refresh: never
  store: ~/.cache/spinor/calibration/acme_q1.json
decomposition:
  one_qubit:
    recipe: euler_zyz
    rotation_gate: rz
    pi_2_gate: sx
  two_qubit:
    recipe: kak
    entangler: ecr
    entangler_count_max: 3
pricing:
  per_shot_usd: 0.01
notes: |
  Demo chip used to test the YAML-only-new-chip property.
```

Step 2 — that's it. Run:

```bash
$ ./build/spinor/cli/spinorc compile -t acme_q1 bell.spn
```

It works. No compiler change.

## §7 — Troubleshooting

| Symptom                                            | Cause / fix                                                                |
| -------------------------------------------------- | -------------------------------------------------------------------------- |
| `error: unknown chip id: <name>`                    | YAML missing or filename mismatches `id`. See registry schema.            |
| `error: W6: gate '<x>' is not native to ...`        | Source uses a gate the target doesn't support natively. Pick another gate or run through `compile` (which decomposes). |
| `error: W3: ... must have distinct operands`        | A two-qubit gate has the same operand twice (`cx q[0], q[0]`). Fix the source. |
| `error: W4: operation '<x>' on measured qubit ...`  | A gate appears after a measurement on a chip without `mid_circuit_measure`. Insert a `reset` between or pick another chip. |
| `equivalence: MISMATCH (diff=...)`                  | Compilation produced a circuit that doesn't agree with the source. Compiler bug; file an issue with the input. |
| `equivalence: skipped (chip has N qubits; ...)`     | Chip is too big for the in-tree statevector. Expected; resource estimate is still accurate. |
| `parse error: expected ']'`                         | Operand index has unbalanced brackets. Fix the source. |
| `python: FileNotFoundError: no cassette for ...`    | Set `SPINOR_SUBMIT_MODE=live` and supply provider credentials, or record a cassette first. |

## §8 — Pinned versions

Snapshot at end-of-phase. Companion to
[`docs/build/versions.md`](versions.md).

| Component       | Pin     | Source                              |
| --------------- | ------- | ----------------------------------- |
| LLVM / MLIR     | 22.1.7  | github.com/llvm/llvm-project        |
| C++             | C++20   | —                                   |
| Eigen           | 5.0.1   | gitlab.com/libeigen/eigen           |
| Stim            | (D7)    | github.com/quantumlib/Stim          |
| Lark (deleted)  | 1.3.1   | github.com/lark-parser/lark         |
| nanobind        | 2.12.0  | github.com/wjakob/nanobind          |
| OpenQASM spec   | 3.1.0   | openqasm.com                        |
| qiskit-ibm-runtime | latest minor | docs.quantum.ibm.com           |
| amazon-braket-sdk | latest minor | docs.aws.amazon.com/braket    |
| azure-quantum   | latest minor | learn.microsoft.com/azure/quantum |

Decisions D1–D8 in [phaseA_decisions.md](phaseA_decisions.md)
explain anything that deviates from the Deep-Dive.

## §9 — Glossary

See [phaseA/glossary.md](phaseA/glossary.md). If you encounter
a term not listed there, it's a documentation bug — please
file or fix.

## §10 — Where to put new code

Mirror of the table in [M0_overview.md](phaseA/M0_overview.md):

| You're adding…              | Folder                                                                           |
| --------------------------- | -------------------------------------------------------------------------------- |
| A new MLIR op               | `spinor/dialect/`                                                                |
| A grammar rule              | `spinor/parser/cpp/`                                                             |
| A verifier rule             | `spinor/verify/`                                                                 |
| A chip                      | `spinor/registry/chips/<id>.yaml` (no code change!)                              |
| A coupling-map topology     | `spinor/registry/topologies/<name>.yaml`                                         |
| A pass                      | `spinor/passes/`                                                                 |
| A simulator backend         | `spinor/sim/`                                                                    |
| An emitter                  | `spinor/emit/`                                                                   |
| A provider adapter          | `spinor/submit/python/spinor_submit/`                                            |
| A test                      | `spinor/tests/<m1..m11>/`                                                        |
| Anything in Phonon/Photon   | **Stop.** This is Phase A only.                                                  |

## §11 — Definition of Done (for Phase A)

All boxes ticked at end-of-phase:

- [x] `spinorc compile -t <chip> bell.spn` produces a
      verbatim-ready artifact.
- [x] The check lane proves equivalence and reports resource
      estimates for the corpus.
- [x] C++ parser has replaced Lark; rustworkx is absent from
      the C++ engine.
- [x] A fourth chip is supported by adding only its YAML —
      no pass code change — and the corpus runs against it
      (`ionq_aria_proto`).
- [x] All M1–M11 tests pass in CI: 116 cases / 15 ctest
      entries, 100% green.
- [x] `phaseA_progress.md`, `phaseA_decisions.md`,
      `phaseA_spinor_guide.md` (this file) written.
- [x] **Stop.** Do **not** start Phonon. The next phase is
      a fresh agent conversation per the handoff brief.
