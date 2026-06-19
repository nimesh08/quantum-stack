# M3 — Verifier (W1–W6 + quantum/classical type check)

## 1. Goal & spec section

The semantic verifier that catches what the grammar (M2) and the
structural verifier (M1) cannot. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 1 §5 — the six
legality rules (W1–W6) plus the lightweight quantum/classical
type check.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:**
  - A verified-by-M1 `Module` (so we can assume well-formed
    operands/types/attributes).
  - A `TargetInfo` snapshot describing the declared target
    chip's native gate set, coupling map, and capability
    flags. Under `target generic`, the snapshot is a
    sentinel (`generic_target()`).
- **Produces:** a `Diagnostics` bag. Each rule emits a
  precise error with the offending op id, source location,
  and a one-line message.
- **Invariants:** if the verifier returns no errors, the
  Module is legal under the declared contract — every
  downstream pass may trust it.

## 3. Deliverables

- `spinor/verify/include/spinor/verify/Verifier.h` — public API
  (`verify(module, target_info, &diag)`).
- `spinor/verify/include/spinor/verify/TargetInfo.h` — minimal
  shim of the M4 registry record (so M3 can ship before M4).
  M4 will provide a concrete loader; M3's tests construct
  `TargetInfo` snapshots inline.
- `spinor/verify/lib/Verifier.cpp` — W1–W6 + type check.
- `spinor/verify/CMakeLists.txt`
- `spinor/tests/m3/CMakeLists.txt`
- `spinor/tests/m3/{w1..w6}_test.cpp` — one test executable per
  rule, each with multiple accept and reject cases.
- `spinor/tests/m3/type_check_test.cpp` — quantum vs classical.
- `spinor/tests/m3/golden_errors_test.cpp` — error messages
  must contain a specific phrase per rule (so users can grep).

## 4. Data structures

```c++
namespace spinor::verify {

// Subset of the future M4 registry sufficient for W4–W6.
struct TargetInfo {
  std::string id;                          // "generic" or "ibm_heron_r2"
  bool generic = true;
  std::vector<std::string> nativeGates;    // empty for generic
  // Coupling map: a sorted vector<pair<int,int>>, undirected
  // (each unordered pair stored once, with low<high). Empty
  // means all-to-all.
  bool allToAll = true;
  std::vector<std::pair<int, int>> coupling;
  std::size_t qubitCount = 0;              // 0 for generic
  bool midCircuitMeasure = true;           // relaxes W4 when true
};

inline TargetInfo generic_target() { return {}; }

}  // namespace spinor::verify

namespace spinor::verify {
void verify(const dialect::Module& m,
            const TargetInfo& target,
            dialect::Diagnostics& diag);
}
```

## 5. Algorithms

Single forward pass with a symbol table:

- **W1 (declare before use).** All operand uses go through
  M1's structural verifier (operand-defined-before-use), so
  W1's only remaining job is the indirect case where a
  measure/reset has happened — handled by W4.
- **W2 (indices in range).** Under a hardware contract, every
  qubit operand's "physical index" — recovered from the
  declaration order in the `alloc_qubit` ops — must be less
  than `target.qubitCount`. Under generic, no bound.
- **W3 (distinct operands in multi-qubit gates).** For every
  op with ≥2 qubit operands, check pairwise distinctness on
  the *root register-element* the operand value descends from
  (each `alloc_qubit` is a unique element).
- **W4 (no operations on a measured qubit until reset).** Track
  per-element "measured" state. `measure` sets it; `reset`
  clears it; any other gate on a measured element is W4
  unless `target.midCircuitMeasure == true`.
- **W5 (portable contract).** Under generic, every gate must
  be in the standard set (`isStandardGate`). Native gates
  (`ecr`, `ms`, `rzz`, `sx`, `sxdg`, `gpi`, `gpi2`, `u1q`)
  are rejected.
- **W6 (hardware contract).** Under a device, every gate name
  must be in `target.nativeGates` (translated to the same
  mnemonic), and every two-qubit gate's operand pair must be
  in `target.coupling` (or the chip is `allToAll`).
- **Type check.** Already enforced by M1's structural verifier
  (gate operand must be Qubit; bit consumption is a separate
  category). M3 adds: a `measure` writes into a bit register
  (not a qubit); a `reset` operand must be a qubit element
  (not a bit).

Implementation details:

- **Tracking elements.** Every `alloc_qubit` is the root of a
  qubit lineage. Each subsequent gate that takes a qubit and
  produces a new one inherits the lineage. We compute a
  `lineage[ValueId] = root_alloc_op_id` during a single
  forward pass.
- **Physical-index map.** Under a hardware contract, the
  k-th `alloc_qubit` corresponds to physical qubit *k*. We
  store this in `physIdx[lineage_root]`.

## 6. Test matrix

| ID    | Name                            | Type     | Inputs                                                    | Expected output                                              | CI gate          |
| ----- | ------------------------------- | -------- | --------------------------------------------------------- | ------------------------------------------------------------ | ---------------- |
| M3-T01 | `M3_w2.in_range_accept`        | unit     | hw target with 4 qubits; ops on 0..3                       | no W2 errors                                                 | `ctest -L M3`    |
| M3-T02 | `M3_w2.out_of_range_reject`    | unit     | hw target with 2 qubits; gate on q[5]                      | error message contains "qubit index"                         | `ctest -L M3`    |
| M3-T03 | `M3_w3.distinct_accept`        | unit     | `cx q[0], q[1]`                                            | no errors                                                    | `ctest -L M3`    |
| M3-T04 | `M3_w3.duplicate_reject`       | unit     | `cx q[0], q[0]`                                            | error contains "distinct operands"                           | `ctest -L M3`    |
| M3-T05 | `M3_w4.measure_then_use_reject` | unit    | gate after measure (chip without mid-circuit support)      | error contains "measured"                                    | `ctest -L M3`    |
| M3-T06 | `M3_w4.measure_then_reset_use` | unit     | measure → reset → gate                                     | accepts                                                      | `ctest -L M3`    |
| M3-T07 | `M3_w4.mid_circuit_relaxes`    | unit     | chip with `midCircuitMeasure=true`; gate after measure     | accepts                                                      | `ctest -L M3`    |
| M3-T08 | `M3_w5.standard_only_accept`   | unit     | `target generic`; standard gates                           | accepts                                                      | `ctest -L M3`    |
| M3-T09 | `M3_w5.native_under_generic_reject` | unit | `target generic`; uses `ecr`                              | error contains "native gate"                                 | `ctest -L M3`    |
| M3-T10 | `M3_w6.native_under_hw_accept` | unit     | `ibm_heron_r2`; uses `ecr` on connected pair               | accepts                                                      | `ctest -L M3`    |
| M3-T11 | `M3_w6.standard_under_hw_reject` | unit   | `ibm_heron_r2`; uses `cx`                                  | error contains "not native"                                  | `ctest -L M3`    |
| M3-T12 | `M3_w6.unconnected_pair_reject` | unit    | `ibm_heron_r2`; `ecr` on disconnected pair                 | error contains "connected pair"                              | `ctest -L M3`    |
| M3-T13 | `M3_w6.alltoall_accept`        | unit     | trapped-ion chip; all-to-all; any pair                     | accepts                                                      | `ctest -L M3`    |
| M3-T14 | `M3_typecheck.measure_into_bit` | unit    | every measure produces a bit; verifier accepts             | accepts                                                      | `ctest -L M3`    |
| M3-T15 | `M3_typecheck.reset_on_qubit_only` | unit | construct reset on a bit (hand-corrupted IR)              | error contains "qubit"                                       | `ctest -L M3`    |
| M3-T16 | `M3_golden_errors.shape`       | golden   | each rule's reject case → message contains the right phrase | grep-like check                                              | `ctest -L M3`    |

## 7. Cookbook example — W4 with and without mid-circuit support

Source under `target ibm_heron_r2` (mid-circuit measure
**supported**):

```
target ibm_heron_r2
qubit q[2]
bit c[1]
h q[0]
c[0] = measure q[0]
h q[0]              ; legal: ibm_heron_r2 supports mid-circuit
```

Source under a fictional `early_chip` (mid-circuit measure
**not** supported):

```
target early_chip
qubit q[2]
bit c[1]
h q[0]
c[0] = measure q[0]
h q[0]              ; W4 error: "operation on measured qubit q[0]"
```

The verifier emits, in the second case:

```
error: W4: operation 'h' on measured qubit q[0]; insert 'reset' first
       (or pick a target whose registry has mid_circuit_measure=true)
```

## 8. Pitfalls

- **Operand lineage.** A two-qubit gate produces two new qubit
  values; you must thread *each* operand's lineage through to
  its specific result. Off-by-one wires q[0]'s lineage to q[1]
  and breaks W3/W4 silently.
- **Generic vs hardware default.** A `TargetInfo` constructed
  with no fields is `generic`; do not infer "hardware" from
  any non-empty field.
- **Native-gate name match.** `target.nativeGates` should hold
  Spinor mnemonics (`"ecr"`, not `"ECR"`); the verifier
  compares strings. The registry schema (M4) enforces lowercase.
- **Coupling map symmetry.** Stored as `(low, high)` ordered
  pairs; the verifier sorts before comparing. Forgetting to
  sort produces phantom W6 errors.
- **Diagnostic stability.** Error messages are graded by golden
  tests (M3-T16). Changing wording is a deliberate
  decision-log entry.

## 9. Definition of Done

- [ ] Spec landed.
- [ ] All M3-T## tests pass.
- [ ] Coverage on `spinor/verify/` ≥ 85%.
- [ ] Test matrix updated.
- [ ] Glossary unchanged (no new terms).
- [ ] Progress journal appended.

## 10. Open questions

None. The W-rules are spec-frozen.
