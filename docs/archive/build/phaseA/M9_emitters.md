# M9 — Emitters (OpenQASM 3 / QIR / Quil)

## 1. Goal & spec section

Three emitters that serialise a chip-locked Spinor IR into the
formats real providers accept. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 3 §4.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

Every emitter produces output ready for **verbatim /
pass-through submission** (Rule 5 — load-bearing).

## 2. Inputs / outputs

- **Consumes:**
  - A `dialect::Module` (typically the output of
    M5+M6+cleanup).
  - Optionally, a `registry::ChipInfo` (used for QIR profile
    selection and OpenQASM verbatim formatting).
  - `EmitOptions` for OpenQASM (`braketVerbatim` toggle).
- **Produces:** `std::string` containing the emitted format.
- **Invariants on output:**
  - QASM3 verbatim: physical-qubit syntax (`$<n>`),
    every gate is in `chip.nativeGates`, every two-qubit
    gate is on a wired pair (verified by tests).
  - QIR: textual LLVM-IR; profile metadata (`base_profile`
    or `adaptive_profile`) matches `chip.supports.feedforward`.
  - Quil: well-formed Quil text; one statement per line;
    `DECLARE ro BIT[N]` for measurements.

## 3. Deliverables

- `spinor/emit/include/spinor/emit/Emitters.h` — public API.
- `spinor/emit/lib/Qasm3.cpp`
- `spinor/emit/lib/Qir.cpp`
- `spinor/emit/lib/Quil.cpp`
- `spinor/emit/CMakeLists.txt`
- `spinor/tests/m9/m9_test.cpp` + `CMakeLists.txt`

## 4. Data structures

```c++
struct EmitOptions {
  bool braketVerbatim = false;     // QASM3 only
};
std::string emitQasm3(const Module&,
                      const ChipInfo* chip = nullptr,
                      EmitOptions = {});
std::string emitQir  (const Module&,
                      const ChipInfo* chip = nullptr);
std::string emitQuil (const Module&);
```

## 5. Algorithms

### Lineage tracking

All three emitters first build `physOf[ValueId] → physical
index` by walking `alloc_qubit` ops in order and propagating
through gate results. This is the same trick M3/M5/M6 use.

### OpenQASM 3.1 emitter

Default form:

```
OPENQASM 3.1;
include "stdgates.inc";
qubit[N] q;
bit[M] c;
<gate>(<angle>) q[i];
<gate> q[i], q[j];
c[k] = measure q[i];
```

Verbatim form (with `chip` + `braketVerbatim=true`):

```
OPENQASM 3.1;
bit[M] c;
#pragma braket verbatim
box {
  ecr $0, $1;
  rz(...) $0;
  ...
  c[0] = measure $0;
}
```

The verbatim wrapper omits the `qubit[N] q;` declaration
(operands are physical addresses) and uses `$<n>` syntax.

### QIR emitter

Textual LLVM-IR with the QIR Alliance function declarations:

```
%Qubit  = type opaque
%Result = type opaque

declare void @__quantum__qis__h__body(%Qubit*)
declare void @__quantum__qis__cnot__body(%Qubit*, %Qubit*)
declare void @__quantum__qis__rx__body(double, %Qubit*)
declare void @__quantum__qis__mz__body(%Qubit*, %Result*)
... (one per gate kind we emit)

define void @main() #0 {
entry:
  call void @__quantum__qis__h__body(inttoptr (i64 0 to %Qubit*))
  call void @__quantum__qis__cnot__body(inttoptr (i64 0 to %Qubit*),
                                        inttoptr (i64 1 to %Qubit*))
  call void @__quantum__qis__mz__body(inttoptr (i64 0 to %Qubit*),
                                      inttoptr (i64 0 to %Result*))
  ret void
}

attributes #0 = { "entry_point"
                  "qir_profiles"="base_profile"
                  "required_num_qubits"="2"
                  "required_num_results"="2" }
```

Profile selection: `chip->supports.feedforward != None`
selects `adaptive_profile`; otherwise `base_profile`.

### Quil emitter

```
DECLARE ro BIT[M]
H 0
CNOT 0 1
MEASURE 0 ro[0]
```

Native gates outside Rigetti's vocabulary (gpi/gpi2/ms/...)
are emitted as `; unsupported op <name>` comments. Quil's
narrow reach is by design (Rigetti devices only).

## 6. Test matrix

| ID | Name | Type | Inputs | Expected | CI gate |
| -- | ---- | ---- | ------ | -------- | ------- |
| M9-T01 | `M9_qasm3.bell_emits_header` | unit | bell.spn | contains `OPENQASM`, `qubit[`, `h q[`, `cx q[`, `measure q[` | `ctest -L M9` |
| M9-T02 | `M9_qasm3.braket_verbatim_box_present` | unit | compiled bell on IBM | contains `#pragma braket verbatim`, `box {`, `$0`, `ecr ` | `ctest -L M9` |
| M9-T03 | `M9_qasm3.round_trip_with_qasm_importer` | integration | bell.spn → emitQasm3 → import via M11 | op count matches | `ctest -L M9` |
| M9-T04 | `M9_qir.bell_emits_base_profile` | unit | bell.spn | QIR text + `base_profile` | `ctest -L M9` |
| M9-T05 | `M9_qir.quantinuum_emits_adaptive_profile` | unit | compiled bell on Quantinuum (Full feedforward) | `adaptive_profile` | `ctest -L M9` |
| M9-T06 | `M9_qir.qubit_count_metadata_present` | unit | ghz.spn | `required_num_qubits`/`required_num_results` set | `ctest -L M9` |
| M9-T07 | `M9_quil.bell_emits_quil` | unit | bell.spn | `H 0`, `CNOT 0 1`, `MEASURE 0 ro[0]` | `ctest -L M9` |
| M9-T08 | `M9_verbatim_invariants.no_standard_gates_in_box` | property | compiled bell on IBM, verbatim | no `cx ` / `h ` inside the box | `ctest -L M9` |

## 7. Cookbook example

```bash
$ spinorc emit -t ibm_heron_r2 -f qasm3 --verbatim bell.spn
OPENQASM 3.1;
bit[2] c;
#pragma braket verbatim
box {
  rz(1.5707963267948966) $0;
  sx $0;
  ecr $0, $1;
  ...
  c[0] = measure $0;
  c[1] = measure $1;
}
```

```bash
$ spinorc emit -t quantinuum_helios -f qir bell.spn | head -3
; Spinor → QIR (Adaptive profile)
; target: quantinuum_helios

%Qubit  = type opaque
```

## 8. Pitfalls

- **Braket verbatim is strict.** Inside `box { ... }` the
  service rejects `cx` even on a connected pair — it must be
  `ecr` (or whatever the chip's native two-qubit is). M9-T08
  enforces this structurally.
- **QIR profile mismatch.** Submitting an Adaptive-profile
  bitcode to a Base-profile-only backend errors out. We
  default to Base unless the chip's `supports.feedforward`
  says otherwise.
- **Quil's narrow reach.** Many of our native gate emissions
  produce `; unsupported op` — that's deliberate; Rigetti
  devices use a different set. Quil is supported because
  Rigetti is reachable through Braket; users targeting
  Rigetti should compile against a Rigetti chip in the
  registry.
- **Round-trip stability.** The QASM importer + emitter
  round-trip is strict on op count but not on whitespace;
  M9-T03 asserts the count match, not text equality.

## 9. Definition of Done

- [x] Spec landed (this file).
- [x] All M9-T## tests green.
- [x] `spinorc emit` subcommand wires QASM3/QIR/Quil.
- [x] Verbatim invariant test in place (M9-T08).

## 10. Open questions

None.
