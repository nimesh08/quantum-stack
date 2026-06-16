# M3 — Linear type system

## 1. Goal & spec section

Implement Phonon's linear type checker. Implements **Phonon
Deep-Dive Part 2** end-to-end: the no-cloning theorem becomes a
compile error. Use-after-measure and implicit-discard are also
caught.

## 2. Inputs / outputs

- **Input.** A `phonon::dialect::Module`, a `verify::TargetInfo`
  (or registry handle so we can probe the W4 relaxation flag),
  and a `Diagnostics` bag.
- **Output.** Diagnostics; `bool typecheck(...)` returns true iff
  no errors were added.

## 3. Deliverables

- `phonon/types/include/phonon/types/LinearTypeChecker.h` — public
  API.
- `phonon/types/lib/LinearTypeChecker.cpp` — implementation.
- `phonon/types/CMakeLists.txt`.
- `phonon/tests/m3/` — legality corpus with golden errors.

## 4. Data structures

```cpp
namespace phonon::types {

struct Options {
  bool warnImplicitDiscard = true;   // discard → warning, not error
  bool midCircuitMeasure   = true;   // honours registry W4 relaxation
};

bool typecheck(const dialect::Module& m,
               const Options& opt,
               dialect::Diagnostics& diag);

// Convenience: derive Options from a TargetInfo.
Options optionsForTarget(const verify::TargetInfo& t);

}  // namespace phonon::types
```

Internal state per-pass:

```cpp
struct ValueState {
  bool consumed = false;     // already used as input to a quantum op
  bool measured = false;     // measure has consumed it (different from
                              //    consumed-by-gate)
  OpId producer;
};
unordered_map<ValueId, ValueState> states;
```

Each consumption increments a use-counter for the value. The
checker reports an error the moment a duplicate use is detected.

## 5. Algorithms

Walk every op top-down. For each op:

1. **Operand pass.** For every operand of qubit type:
   - If `state.consumed`, emit
     `error E1: qubit value '%v' used more than once (no-cloning)`.
   - Else if `state.measured` and the gate is anything other than
     `reset` (and the W4 relaxation is off): emit
     `error E2: qubit '%v' used after measurement`.
   - Else mark `consumed = true`.
2. **Result pass.** For every qubit-typed result of the op,
   create a fresh `ValueState` with `consumed = false`,
   `measured = false`. Special case: `Measure` consumes its
   qubit operand and produces a `bit` (no new qubit value);
   `Reset` consumes the operand qubit (and clears `measured`)
   and produces a fresh qubit value with all flags clear —
   *unless* the input was not yet measured, in which case the
   reset still proceeds (it's destructive prep) but we emit a
   warning that an unmeasured qubit is being reset.
3. **Region descent.** `phonon.if` and `phonon.for` and
   `phonon.while` introduce new scopes. The qubit values used
   inside a region must come from outside (linear), and any
   value defined inside the region cannot escape (the region
   must produce/return them through op results — out of scope
   for M3). For now: iterate the region body in source order.
4. **Final discard pass.** Walk every fresh qubit value; if
   `consumed == false` AND `measured == false` at end-of-module
   AND the value has no successor op consuming it, emit
   `warning W: qubit produced by '<mnemonic>' is implicitly
   discarded`.

The W4 relaxation: if `Options::midCircuitMeasure == true`, the
checker treats post-measure usage as legal *only* via `reset`;
arbitrary gates after measure remain illegal (this matches Phase
A's M3 verifier).

## 6. Test matrix

| Test name | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M3_linear.bell_passes` | unit | builder bell module | no errors. |
| `M3_linear.no_cloning_caught` | unit | manually-built bad IR using `q0` twice | error E1 emitted. |
| `M3_linear.use_after_measure_caught` | unit | gate after measure (no W4) | error E2 emitted. |
| `M3_linear.use_after_measure_with_w4` | unit | reset between measure and gate | accepted. |
| `M3_linear.implicit_discard_warns` | unit | qubit prepared, never measured, never reset | one warning. |
| `M3_linear.teleportation_passes` | integration | parsed `teleportation.phn` | no errors. |
| `M3_linear.bell_pair_func_passes` | integration | parsed `bell_pair_func.phn` | no errors. |
| `M3_linear.qft_loop_passes` | integration | parsed `qft_loop.phn` | no errors. |
| `M3_linear.classical_no_op` | unit | int / angle classical values used many times | no errors (non-linear). |

## 7. Cookbook example

```
target generic
qubit q[1]
h q[0]
h q[0]
```

Spinor's M3 verifier accepts this (the parser threads SSA values
through the slot bindings, so the second `h q[0]` operates on the
new value produced by the first `h`). To trigger the cloning
error you must build the IR by hand using a stale ValueId:

```cpp
pd::Module m;
pd::Builder b(m);
auto q0 = b.allocQubit();
auto q0a = b.h(q0);
auto q0b = b.h(q0);  // BUG: reuses q0, not q0a
EXPECT_ERROR_E1();
```

## 8. Pitfalls

- **Region operands.** A `phonon.if` consumes its predicate,
  which is a `bit`. Bits are non-linear, so they may be
  reused. Don't accidentally mark the predicate operand as
  consumed.
- **Function bodies.** Inside a `phonon.def`, parameter
  qubit values are alive at entry and consumed by ops in the
  body; they must end up flowing into the `phonon.return` (or
  measure / reset / discard). The checker honours that walk.
- **Slot rebinding through gates is *fine*.** The parser
  already does this; the checker just sees fresh ValueIds at
  each rewrite.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M3`.
- [ ] Phase A tests still green.
- [ ] `phaseB_progress.md` entry appended.

## 10. Open questions

None.
