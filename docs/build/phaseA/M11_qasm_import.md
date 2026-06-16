# M11 — OpenQASM 3 Subset Importer

## 1. Goal & spec section

A small, focused importer that consumes the strict subset of
OpenQASM 3.1 that maps 1:1 to our grammar, and produces a
`dialect::Module`. Round-trips with the M9 emitter.

Implements the import-door responsibility called out in
[Spinor Engineering Deep-Dive][deep-dive] §1 (Vision §3.5)
and Decision D1 in
[phaseA_decisions.md](../phaseA_decisions.md).

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:** OpenQASM 3 source text matching the strict
  subset.
- **Produces:** a `dialect::Module` (or a `Diagnostics` with
  precise errors).
- **Invariants on output:** every op corresponds 1:1 to a
  source statement; the module's target attribute is whatever
  the caller passes (default `generic`).

## 3. Subset definition

The supported constructs are exactly:

```
OPENQASM 3.x;                     -- ignored (shape preamble)
include "stdgates.inc";           -- ignored

qubit[N] q;                       -- == `qubit q[N]`
bit[N]   c;                       -- == `bit c[N]`

<gate>[(angles...)] q[i];                       -- 1q gate
<gate>[(angles...)] q[i], q[j];                 -- 2q gate

c[i] = measure q[i];              -- per-element measure

reset q[i];                       -- reset
barrier q[i] [, q[j]] ...;        -- barrier
```

Where `<gate>` is one of the Spinor mnemonics
(`h`, `x`, `y`, `z`, `s`, `sdg`, `t`, `tdg`, `rx`, `ry`,
`rz`, `cx`, `cz`, `swap`, `ecr`, `ms`, `rzz`, `sx`, `sxdg`,
`gpi`, `gpi2`).

Anything outside this subset is rejected with a precise
error directing the user to write Phonon for control flow:

- `gate <name>(...) ... { ... }` — a gate definition.
  Rejected: "Phonon (Phase B) is for control flow / gate
  definitions".
- `if (...) { ... }` — control flow. Rejected.
- `for ...` — control flow. Rejected.
- `def <name>(...) ... { ... }` — subroutine. Rejected.
- `defcal ...` — pulse-level calibration. Rejected.

## 4. Deliverables

- `spinor/parser/qasm/include/spinor/parser/Qasm.h` — public
  `import(text, filename, target) → ImportResult`.
- `spinor/parser/qasm/lib/Qasm.cpp` — line-oriented importer.
- `spinor/parser/qasm/CMakeLists.txt`.
- `spinor/tests/m11/qasm_corpus/` — fixtures.
- `spinor/tests/m11/m11_test.cpp` + `CMakeLists.txt`.

## 5. Algorithms

The importer is line-oriented and tolerates `;`-separated
statements per line. For each logical statement:

1. Strip `// ...` comments (single-line only).
2. Strip the trailing `;`.
3. Match the leading keyword against `OPENQASM`, `include`,
   `qubit`, `bit`, `reset`, `barrier`.
4. If the line contains `=` (and isn't a declaration), it's
   a measurement assignment.
5. Otherwise it's a gate statement; first identifier is the
   gate name, optional `(...)` is angle params, rest are
   `name[idx]` operands.

Angle evaluation matches the C++ parser exactly:

| Form         | Evaluation        |
| ------------ | ----------------- |
| `pi`         | `M_PI`            |
| `-pi`        | `-M_PI`           |
| `pi/N`       | `M_PI / N`        |
| `-pi/N`      | `-M_PI / N`       |
| `R*pi`       | `R * M_PI`        |
| `-R*pi`      | `-R * M_PI`       |
| `<decimal>`  | `stod(...)`       |

## 6. Test matrix

| ID | Name | Type | Inputs | Expected | CI gate |
| -- | ---- | ---- | ------ | -------- | ------- |
| M11-T01 | `M11_qasm.bell_imports` | unit | `bell.qasm` | parses; H + CX + 2 measure | `ctest -L M11` |
| M11-T02 | `M11_qasm.ghz_imports` | unit | `ghz.qasm` | 2 CX, 3 measure | `ctest -L M11` |
| M11-T03 | `M11_qasm.rotations_imports` | unit | `rotations.qasm` | `pi/2`, `-pi`, `0.5*pi` evaluate correctly | `ctest -L M11` |
| M11-T04 | `M11_qasm.mid_circuit_imports` | unit | `mid_circuit.qasm` | mid-stream measure + reset | `ctest -L M11` |
| M11-T05 | `M11_qasm.rejects_gate_definitions` | regression | `unsupported_gate_def.qasm` | error mentions "Phonon" | `ctest -L M11` |
| M11-T06 | `M11_qasm.rejects_control_flow` | regression | `unsupported_if.qasm` | rejected | `ctest -L M11` |
| M11-T07 | `M11_qasm.parity_with_spn_bell` | integration | `bell.qasm` + `bell.spn` | same op count | `ctest -L M11` |

## 7. Cookbook example

`bell.qasm` →

```
OPENQASM 3.0;
include "stdgates.inc";
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
c[0] = measure q[0];
c[1] = measure q[1];
```

The importer returns a `Module` structurally identical to what
the SPN parser produces for `bell.spn`. M9's emitter +
M11's importer form a round-trip property tested in M9-T03.

## 8. Pitfalls

- **OpenQASM 3 has no fixed semicolon-vs-newline rule.** We
  accept either; statements are split on top-level `;`.
- **Whole-register operands.** `cx q, p` is whole-register
  syntax; we reject it (Phonon territory) — every operand
  must be `name[idx]`.
- **`include "stdgates.inc"`** is ignored, not actually
  loaded. We assume the gate names are the standard set.
- **Pragmas / annotations.** Currently ignored if encountered
  outside a gate context; future versions may honour
  `@spinor.target = "..."` annotations.

## 9. Definition of Done

- [x] Spec landed (this file).
- [x] All M11-T## tests green.
- [x] Subset definition documented.
- [x] Test matrix updated.
- [x] Round-trip property tested in M9-T03.

## 10. Open questions

None.
