# M1 — Phonon dialect (extends Spinor)

## 1. Goal & spec section

Implement the Phonon MLIR dialect as a strict extension of the
Spinor dialect from Phase A. Implements **Phonon Deep-Dive Part 1
§§3.3, 4** (the dialect is what the lowering passes operate on).

## 2. Inputs / outputs

- **Inputs.** None at runtime. The dialect is a library used by
  every later milestone (M2 parser, M3 type checker, M4 lowering,
  M5/M6 optimizer).
- **Outputs.** A C++ library `phonon::dialect` exposing types,
  ops, builder, printer, parser, structural verifier, identical
  in shape to `spinor::dialect`.

## 3. Deliverables

- `phonon/dialect/include/phonon/dialect/Phonon.h` — public API.
- `phonon/dialect/lib/Phonon.cpp` — in-tree backend.
- `phonon/dialect/lib/Print.cpp` — textual printer (round-trippable).
- `phonon/dialect/lib/Parse.cpp` — textual parser.
- `phonon/dialect/lib/Verify.cpp` — structural verifier.
- `phonon/dialect/td/PhononDialect.td`,
  `td/PhononTypes.td`, `td/PhononOps.td` — TableGen sources for the
  MLIR-backed bridge (consumed when `QSTACK_HAVE_LLVM` is ON).
- `phonon/dialect/CMakeLists.txt` — `phonon::dialect` target,
  links to `spinor::dialect`.
- `phonon/CMakeLists.txt` — re-enables `add_subdirectory(dialect)`.
- `phonon/tests/m1/` — four executables (builder, round-trip,
  verify, golden) plus goldens.

## 4. Data structures

The Phonon dialect re-exports Spinor's `Type`, `OpKind`, `ValueId`,
`Op`, `Attribute` shapes via using-declarations and adds:

```cpp
namespace phonon::dialect {

using spinor::dialect::Attribute;
using spinor::dialect::Diagnostics;
using spinor::dialect::Location;

enum class TypeKind : uint8_t { Qubit, Bit, Int, Angle, Func };

struct Type { TypeKind kind; };

enum class OpKind : uint16_t {
  // re-export every Spinor::OpKind under matching names
  AllocQubit, AllocBit, H, X, ..., Barrier,
  // Phonon additions
  ConstInt,        // attr "value" : int
  ConstAngle,      // attr "value" : double (radians)
  BinOp,           // attr "op" : "+|-|*|/"; 2 int/angle operands → 1 result
  Cmp,             // attr "op" : "==|!=|<|>"; 2 operands → 1 bit result
  If,              // operand: predicate (bit/int); body: contiguous ops
                   // marker; "then_count"/"else_count" attrs say how many
                   // ops in each branch. (See § Region encoding.)
  EndIf,
  For,             // loop_var; lo, hi compile-time int operands; body marker
  EndFor,
  While,           // predicate-symbol; lowered or rejected later
  EndWhile,
  Def,             // function definition marker; "name", "param_count",
                   //   "body_count" attrs; param types via attrs.
  EndDef,
  Call,            // attr "name"; operands = args; results = returned
                   //   qubits / bits / classical scalars.
  Return,          // results: classical or qubit values to return.
  Assign,          // operand: source value; attr "name" (symbol).
};

}  // namespace phonon::dialect
```

### Region encoding

The in-tree backend has a flat op vector; MLIR regions are
re-encoded as marker-pair ops. `phonon.if` is paired with
`phonon.end_if`; `phonon.for` with `phonon.end_for`; `phonon.def`
with `phonon.end_def`. Counts are also stored as attributes for
fast lookup. The MLIR-backed bridge will translate these to real
`Region`s. The flat encoding is what lets the Phase A printer
shape transfer cleanly; it also means our in-tree verifier is
linear-time.

### Module type

```cpp
class Module {
 public:
  std::string targetAttr;  // "generic" or chip id
  std::string name;        // "main"
  ValueId addValue(Type t, OpId producer);
  OpId addOp(Op op);
  // ... (mirrors spinor::dialect::Module)
};
```

## 5. Algorithms

### Builder

A subclass-style `Builder` mirrors the Spinor one (section
references in `lib/Phonon.cpp`). The Spinor surface (gates,
measure, reset, alloc) is implemented by *delegation*: the in-tree
backend stores both Spinor and Phonon ops in the same op table
(distinguished by mnemonic prefix `spinor.` vs `phonon.`).

### Printer / Parser

MLIR-style textual format, deliberately a strict superset of the
Spinor textual format. Key shape:

```
phonon.module @main attributes {target = "generic"} {
  %q0 = spinor.alloc_qubit : !spinor.qubit
  %i0 = phonon.const_int {value = 0} : !phonon.int
  phonon.for %i = %i0 .. %i2 {
    %q0_1 = spinor.h %q0 : !spinor.qubit
  }
  ...
}
```

The parser is deliberately small (the production parser is in
M2). M1 ships a textual parser only for round-trip testing.

### Structural verifier

For every op:
- attributes match the op signature;
- operand types match the op's expected types;
- markers are paired (if/end_if, for/end_for, def/end_def);
- `phonon.def` body contains no nested `phonon.def`;
- `phonon.return` only inside a `phonon.def`.

Semantic checks (linear typing) live in M3, not here.

## 6. Test matrix

| Test | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M1_builder.bell_in_phonon` | unit | builder API | bell-state IR has 5 ops (alloc_qubit×2, alloc_bit, h, cx, measure-implicit); types correct. |
| `M1_builder.teleportation_skeleton` | unit | builder + control-flow ops | 3-qubit teleportation IR builds with `if`, mid-circuit measure, classical-controlled gates. |
| `M1_builder.const_and_binop` | unit | builder API | `const_int(2) + const_int(3)` produces `phonon.binop` with int result. |
| `M1_builder.def_call_return` | unit | builder API | Function with two qubit params, body inlined ready, paired markers. |
| `M1_round_trip.bell_phonon` | integration | print → parse → print | byte equality. |
| `M1_round_trip.teleportation` | integration | print → parse → print | byte equality. |
| `M1_round_trip.for_with_const_bounds` | integration | print → parse → print | byte equality. |
| `M1_verify.unpaired_if_rejected` | unit | hand-built bad IR | structural verifier reports "phonon.if missing end_if". |
| `M1_verify.return_outside_def_rejected` | unit | hand-built bad IR | structural verifier rejects. |
| `M1_verify.cmp_predicate_type` | unit | hand-built bad IR | `phonon.if` operand must be bit or int, not qubit. |
| `M1_print_golden.bell_phonon` | golden | builder | matches `goldens/bell.phn` byte-equally. |
| `M1_print_golden.teleportation` | golden | builder | matches `goldens/teleportation.phn`. |

## 7. Cookbook example

```cpp
phonon::dialect::Module m;
m.targetAttr = "generic";
phonon::dialect::Builder b(m);
auto q0 = b.allocQubit();
auto q1 = b.allocQubit();
auto c0 = b.allocBit();
auto c1 = b.allocBit();
auto q0a = b.h(q0);
auto [q0b, q1a] = b.cx(q0a, q1);
auto bit0 = b.measure(q0b);
auto bit1 = b.measure(q1a);
// classical-controlled X on q1 (teleportation pattern)
auto pred = b.cmp("==", bit0, b.constInt(1));
auto ifId = b.beginIf(pred);
//   body
b.x(q1a);  // (would be a fresh value; example omits the rebinding)
b.endIf(ifId);
```

Resulting IR (round-trip through the printer):

```
phonon.module @main attributes {target = "generic"} {
  %q0 = spinor.alloc_qubit : !spinor.qubit
  ...
}
```

## 8. Pitfalls

- **Marker pairing.** Forgetting to call `b.endIf(...)` leaves a
  dangling `phonon.if`. Caught by `M1_verify.unpaired_if_rejected`.
- **Attribute key drift.** Phonon adds new attribute keys (`op`,
  `value`, `name`, `param_count`, `body_count`, `then_count`,
  `else_count`); the structural verifier must validate them per
  op kind, not silently accept everything.
- **Op-table interleaving.** The in-tree backend stores Spinor
  and Phonon ops in one op vector; printers must call
  `spinor::dialect::opMnemonic(...)` for Spinor-kind ops and
  `phonon::dialect::opMnemonic(...)` for Phonon-kind ops. Caught
  by golden tests.
- **Type round-trip.** `!phonon.int` and `!phonon.angle` must be
  printed and parsed in the result-type list of every op that
  produces them; otherwise the parser cannot reconstruct the
  Module.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L M1`.
- [ ] `phonon::dialect` builds without `QSTACK_HAVE_LLVM`.
- [ ] TableGen sources committed in `phonon/dialect/td/`.
- [ ] `phonon/dialect/include/phonon/dialect/Phonon.h` does not
      include any LLVM/MLIR header.
- [ ] `phaseB_progress.md` entry appended.

## 10. Open questions

None. Proceed.
