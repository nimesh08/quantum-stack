# M1 — Spinor MLIR Dialect (types, ops, IR data structure)

## 1. Goal & spec section

Define the Spinor IR — the data structure every later pass
rewrites. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 1 §6 (and lays
the groundwork for §3 grammar and §5 verifier in M2/M3).

Two types: `!spinor.qubit` and `!spinor.bit`. Ops for every
gate, plus `alloc_qubit`, `alloc_bit`, `measure`, `reset`,
`barrier`, and a module-level `spinor.target` attribute.
Value-semantics throughout (every gate consumes a qubit value
and produces a new one) — this is what makes accidental
re-use of a measured qubit a detectable error and is the
foundation no-cloning later rests on.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:** nothing (M1 is the foundation). Hand-written IR
  used in tests is built directly via the C++ Builder API.
- **Produces:** the public C++ API
  `spinor::dialect::*` (`Module`, `Op`, `Qubit`, `Bit`,
  `OpKind`, `Builder`), plus the textual MLIR round-trip
  (when LLVM is present) and an in-tree textual format
  (always available) for golden tests.
- **Invariants on output:**
  - Every gate op consumes its qubit operand(s) and produces
    fresh qubit result(s) (SSA / value-semantics).
  - The module attribute `spinor.target` is always set (either
    `"generic"` or a device id).
  - Bit values are produced only by `measure`; gates only
    consume `Qubit` values, never `Bit`.

## 3. Deliverables

Files added under
[`spinor/`](../../../spinor):

- `spinor/dialect/CMakeLists.txt`
- `spinor/dialect/include/spinor/dialect/Spinor.h`
  — public C++ API (op kinds, type kinds, Module, Op, Qubit,
  Bit, Builder, printer/parser).
- `spinor/dialect/include/spinor/dialect/SpinorOps.h`
  — convenience constructors per op (`buildH`, `buildCx`, …).
- `spinor/dialect/lib/Spinor.cpp` — in-tree backend
  implementation (always built).
- `spinor/dialect/lib/Print.cpp` — textual format printer.
- `spinor/dialect/lib/Parse.cpp` — textual format parser
  (consumed by round-trip tests; not the source-language
  parser — that is M2/M7).
- `spinor/dialect/lib/Verify.cpp` — well-formedness checks
  (a different layer from M3's W1-W6: this one only checks
  the IR is structurally consistent — every operand has the
  right type, every result is unique, etc.).
- `spinor/dialect/td/SpinorDialect.td` — TableGen dialect.
- `spinor/dialect/td/SpinorOps.td` — TableGen ops.
- `spinor/dialect/td/SpinorTypes.td` — TableGen types.
- `spinor/dialect/mlir/SpinorMLIR.cpp` — MLIR-backed
  backend (built only when `QSTACK_HAVE_LLVM` is ON; converts
  in-tree IR to MLIR ops and back, and registers the dialect
  with the MLIR context).
- `spinor/tests/m1/CMakeLists.txt`
- `spinor/tests/m1/round_trip_test.cpp` — round-trip the Bell
  program through the textual format.
- `spinor/tests/m1/builder_test.cpp` — IR builder API.
- `spinor/tests/m1/verify_test.cpp` — structural verifier
  rejects bad ops with precise messages.
- `spinor/tests/m1/print_golden_test.cpp` — printed text
  matches a golden file for several reference programs.
- `spinor/tests/m1/goldens/bell.spnir`
- `spinor/tests/m1/goldens/ghz.spnir`
- `spinor/tests/m1/goldens/rotations.spnir`

## 4. Data structures

### Types

```c++
namespace spinor::dialect {

enum class TypeKind : uint8_t { Qubit, Bit };

struct Type {
  TypeKind kind;
  bool operator==(const Type&) const = default;
};

inline constexpr Type qubitType() { return {TypeKind::Qubit}; }
inline constexpr Type bitType()   { return {TypeKind::Bit};   }

}  // namespace spinor::dialect
```

### Op kinds

```c++
enum class OpKind : uint16_t {
  // memory
  AllocQubit, AllocBit,
  // single-qubit standard gates
  H, X, Y, Z, S, Sdg, T, Tdg,
  // single-qubit rotations
  Rx, Ry, Rz,
  // two-qubit standard gates
  Cx, Cz, Swap,
  // native gates (legal only under HW contract; verifier in M3 enforces)
  Ecr, Ms, Rzz, Sx, Sxdg, Gpi, Gpi2, U1q,
  // measurement / reset / barrier
  Measure, Reset, Barrier,
};
```

### Op

An Op is the unit of IR. Every Op has:

- `OpKind kind` — discriminator.
- `std::vector<ValueId> operands` — input values.
- `std::vector<ValueId> results` — output values, all newly
  allocated.
- `std::vector<Attribute> attributes` — compile-time
  constants. For Phase A, only rotation angles use this; an
  attribute is `{ name: std::string, value: double }`.
- `Location loc` — source location (file, line, col) for
  diagnostics. `{}` if unknown.

`ValueId` is a strong typedef over `uint32_t`. Every value is
allocated exactly once; the Module owns the value table and
keeps a `Type` per value.

### Module

```c++
class Module {
 public:
  // attributes
  std::string targetAttr;             // "generic" or a device id

  // value table: kind per value, and the op that produced it
  ValueId addValue(Type t, OpId producer);
  Type    typeOf(ValueId) const;
  OpId    producerOf(ValueId) const;

  // op table (flat, in program order)
  OpId    addOp(Op op);
  const Op& op(OpId id) const;
  Op&       opMut(OpId id);
  std::span<const Op> ops() const;

  // helpers used by tests and passes
  std::vector<ValueId> usedBy(ValueId) const;
};
```

The flat-op-list form matches the spec (Spinor is assembly,
no control flow at this layer; Phonon will introduce regions
for control flow). Iteration is forward, in program order.

### Builder

```c++
class Builder {
 public:
  explicit Builder(Module& m);
  Module& module();

  // declarations
  ValueId allocQubit(Location loc = {});
  ValueId allocBit(Location loc = {});

  // single-qubit gates: each consumes one Qubit, produces a new Qubit
  ValueId h(ValueId q, Location loc = {});
  ValueId x(ValueId q, Location loc = {});
  // … one method per OpKind in the gate set …
  ValueId rz(double angle, ValueId q, Location loc = {});

  // two-qubit gates
  std::pair<ValueId, ValueId> cx(ValueId ctrl, ValueId tgt, Location = {});

  // measurement / reset / barrier
  ValueId measure(ValueId q, Location loc = {});  // returns the Bit
  ValueId reset(ValueId q, Location loc = {});    // returns the fresh Qubit
  void    barrier(std::vector<ValueId> qs, Location loc = {});
};
```

### TableGen mapping (when LLVM is present)

Each in-tree `OpKind` maps 1:1 to an MLIR op defined in
`SpinorOps.td`. The mapping is direct: `OpKind::H` ↔
`spinor.h`, `OpKind::Cx` ↔ `spinor.cx`, etc. The
MLIR-backed backend translates `spinor::dialect::Module` →
`mlir::ModuleOp` (and back) using this table.

## 5. Algorithms

There is no algorithmic content in M1 beyond bookkeeping.
Three small ones:

- **Value allocation.** Monotonic counter; never reused. Used
  by tests to assert SSA freshness.

- **Textual round-trip.**
  - Print: visit ops in program order. For each: print
    operands by `%name`, attributes in `()`, kind keyword,
    then results. The format is deliberately MLIR-like:
    `%out = spinor.h %in : !spinor.qubit`.
  - Parse: line-oriented LL(1). Reject malformed text with
    `Diagnostic{ line, col, message }`.

- **Structural verify.** Forward sweep:
  - Every operand id is defined before use.
  - Every operand's `Type` matches the op's signature.
  - Result types match the op's signature.
  - Per-op attribute keys match the op's expected set
    (e.g. `Rz` requires exactly one `angle`).
  - The `spinor.target` module attribute is set.

  Errors are returned as a `std::vector<Diagnostic>`; M3 layers
  the *semantic* W1-W6 verifier on top.

## 6. Test matrix

| ID    | Name                                | Type     | Inputs                              | Expected output                                                       | CI gate                       |
| ----- | ----------------------------------- | -------- | ----------------------------------- | --------------------------------------------------------------------- | ----------------------------- |
| M1-T01 | builder.bell_module                | unit     | Builder API calls for Bell program  | Module has 2 alloc_qubit, 2 alloc_bit, 1 h, 1 cx, 2 measure, target generic | `ctest -L M1`            |
| M1-T02 | builder.value_uniqueness           | property | random builder usage (1000 cases)   | every produced value id is unique and used exactly once               | `ctest -L M1`                 |
| M1-T03 | builder.type_signatures            | unit     | each gate op constructed            | operand and result types match the op's signature                     | `ctest -L M1`                 |
| M1-T04 | print.bell_matches_golden          | golden   | Bell module                         | printed text equals `goldens/bell.spnir` byte-for-byte                | `ctest -L M1`                 |
| M1-T05 | print.ghz_matches_golden           | golden   | 3-qubit GHZ module                  | printed text equals `goldens/ghz.spnir`                               | `ctest -L M1`                 |
| M1-T06 | print.rotations_matches_golden     | golden   | rotations module (rx, ry, rz, t, s) | printed text equals `goldens/rotations.spnir`                         | `ctest -L M1`                 |
| M1-T07 | round_trip.bell                    | property | Bell module                         | print → parse → print is a fixed point                                | `ctest -L M1`                 |
| M1-T08 | round_trip.ghz                     | property | GHZ module                          | print → parse → print is a fixed point                                | `ctest -L M1`                 |
| M1-T09 | round_trip.rotations               | property | rotations module                    | print → parse → print is a fixed point                                | `ctest -L M1`                 |
| M1-T10 | round_trip.fuzz                    | fuzz     | random valid IR (1000 cases)        | print → parse → print is a fixed point on every case                  | `ctest -L M1`                 |
| M1-T11 | verify.accept_well_formed          | unit     | Bell, GHZ, rotations modules        | structural verify returns no diagnostics                              | `ctest -L M1`                 |
| M1-T12 | verify.reject_undefined_operand    | unit     | Op referencing unknown ValueId      | precise diagnostic with op index and bad operand                      | `ctest -L M1`                 |
| M1-T13 | verify.reject_type_mismatch        | unit     | gate consuming a `!spinor.bit`      | precise diagnostic naming expected and actual types                   | `ctest -L M1`                 |
| M1-T14 | verify.reject_missing_attribute    | unit     | `Rz` without an `angle` attr        | precise diagnostic                                                    | `ctest -L M1`                 |
| M1-T15 | verify.reject_no_target            | unit     | Module with empty target attr       | precise diagnostic                                                    | `ctest -L M1`                 |
| M1-T16 | verify.reject_duplicate_result_id  | unit     | hand-corrupted Op                   | precise diagnostic                                                    | `ctest -L M1`                 |
| M1-T17 | mlir_backend.dialect_registers     | unit     | (LLVM build only)                   | MLIRContext loads `spinor` dialect; module verifies                   | `ctest -L M1 -L MLIR`         |
| M1-T18 | mlir_backend.bell_lowers_and_back  | integration | (LLVM build only) Bell module    | in-tree → MLIR → in-tree is identity                                  | `ctest -L M1 -L MLIR`         |

`-L MLIR` jobs are guarded; in CI the MLIR-backed jobs run on
the LLVM-equipped image, the rest run on the bare image.

## 7. Cookbook example — the Bell program in IR

Source (M2 will parse this; for now we build it via the
Builder API):

```spn
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

Built in C++:

```c++
spinor::dialect::Module m;
m.targetAttr = "generic";
spinor::dialect::Builder b(m);

auto q0 = b.allocQubit();
auto q1 = b.allocQubit();
auto c0 = b.allocBit();   // declared but assigned by `measure`
auto c1 = b.allocBit();
auto q0a = b.h(q0);
auto [q0b, q1a] = b.cx(q0a, q1);
auto bit0 = b.measure(q0b);  // c0 -> bit0
auto bit1 = b.measure(q1a);
```

Printed in the in-tree textual format:

```
spinor.module @main attributes {target = "generic"} {
  %q0 = spinor.alloc_qubit : !spinor.qubit
  %q1 = spinor.alloc_qubit : !spinor.qubit
  %c0 = spinor.alloc_bit   : !spinor.bit
  %c1 = spinor.alloc_bit   : !spinor.bit
  %q0_1 = spinor.h %q0 : !spinor.qubit
  %q0_2, %q1_1 = spinor.cx %q0_1, %q1 : !spinor.qubit, !spinor.qubit
  %c0_1 = spinor.measure %q0_2 : !spinor.bit
  %c1_1 = spinor.measure %q1_1 : !spinor.bit
}
```

The format is deliberately MLIR-textual-style so the
MLIR-backed backend can use MLIR's parser/printer when
available. The in-tree backend implements the same
syntax, so tests written against either produce the same
strings.

What changed at each step:

1. `target generic` → module attribute.
2. `qubit q[2]` → two `alloc_qubit` ops.
3. `bit c[2]` → two `alloc_bit` ops.
4. `h q[0]` → `spinor.h` consumes `%q0` and produces `%q0_1`
   (SSA: the post-Hadamard qubit value).
5. `cx q[0], q[1]` → two-result op consumes `%q0_1, %q1` and
   produces `%q0_2, %q1_1`. Notice each qubit gets a fresh
   value.
6. `c = measure q` → two `spinor.measure` ops, each consuming
   a qubit value and producing a bit value.

## 8. Pitfalls

- **`assemblyFormat` mismatch in TableGen.** When LLVM is
  present, MLIR's text format is driven by each op's
  `assemblyFormat`. If the in-tree printer and MLIR's printer
  disagree, M1-T18 catches it. Pitfall: forgetting to print
  result types on multi-result ops produces ambiguous text.
- **SSA leak across measure/reset.** A measurement consumes a
  qubit and produces a bit. Subsequent code must not use the
  pre-measure qubit value — and must not use the bit as a
  qubit. M1-T13 catches the latter; M3's W4 catches the
  former.
- **Angle attribute precision.** Attributes are `double`;
  printed with full precision (`std::to_chars` round-trippable).
  M1-T06 catches lossy printers via byte-for-byte golden.
- **Value-id reuse.** The id allocator must be monotonic.
  Recycling an id breaks SSA reasoning. M1-T02 catches it.
- **Empty target attribute.** Easy to forget to set in tests.
  M1-T15 catches it; the Builder constructor leaves the
  attribute empty so users must set it explicitly (no silent
  default).
- **MLIR header pollution.** The public
  `spinor::dialect::*` header must NOT include any MLIR
  headers — that would force everyone to depend on LLVM.
  MLIR-only code lives behind `lib/mlir/SpinorMLIR.cpp` and
  is gated by `QSTACK_HAVE_LLVM`.

## 9. Definition of Done

- [x] Spec landed (this file).
- [ ] All M1-T## unit/integration/property/golden tests pass
      locally on the in-tree backend.
- [ ] `M1-T17` and `M1-T18` pass in the MLIR-equipped CI job
      (or are skipped with a clear message when LLVM is not
      present).
- [ ] Coverage on `spinor/dialect/` ≥ 85%.
- [ ] Test matrix
      ([test-matrix.md](test-matrix.md)) updated with all 18
      rows.
- [ ] Glossary stays accurate (no new terms beyond those
      already covered).
- [ ] Progress journal appended.
- [ ] No deviations from the Deep-Dive (decisions log not
      updated for M1).

## 10. Open questions

None blocking. M1 has no dependency on the registry, the
parser, or the verifier's W-rules.
