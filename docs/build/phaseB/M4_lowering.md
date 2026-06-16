# M4 — Lowering Phonon → Spinor

## 1. Goal & spec section

Implement the Phonon-to-Spinor lowering pass. Implements **Phonon
Deep-Dive Part 1 §3** (the bridge between layers).

Three transformations: **function inlining**, **loop unrolling**,
**conditional flattening**. After the pass runs, the resulting
module contains only Spinor-kind ops and is byte-equivalent to a
hand-written `.spn` source.

## 2. Inputs / outputs

- **Input.** A `phonon::dialect::Module` plus an optional
  `verify::TargetInfo` (used for W4-aware conditional handling).
- **Output.** A `spinor::dialect::Module` whose `targetAttr` is
  copied from the Phonon module and whose ops are exclusively
  `spinor.*`. Diagnostics for any unsupported pattern.

## 3. Deliverables

- `phonon/lower/include/phonon/lower/Lowering.h` — public API.
- `phonon/lower/lib/Lowering.cpp` — implementation.
- `phonon/lower/CMakeLists.txt`.
- Tests in `phonon/tests/m4/`:
  - **Worked example.** `bell_pair_func.phn` lowers to byte-stable
    flat Spinor.
  - **Loop unroll.** `qft_loop.phn` lowers to four `spinor.h` ops.
  - **Conditional flatten.** `teleportation.phn` lowers to a flat
    sequence with each `if`-body spliced in (W4 chip is required;
    lowering rejects when it would produce post-measure gates on a
    chip without the W4 flag).
  - **Containment / Phase A pipeline.** Lowered output runs through
    Phase A `verify` + `compile` + `check` unchanged.

## 4. Data structures

```cpp
namespace phonon::lower {

struct Result {
  std::optional<spinor::dialect::Module> module;
  spinor::dialect::Diagnostics diag;
};

Result lower(const phonon::dialect::Module& m,
             const spinor::verify::TargetInfo* target = nullptr);

}  // namespace phonon::lower
```

Internal:

- `valueMap : phonon::ValueId → spinor::ValueId`. Threaded
  through walking; updates whenever a slot binding is rewritten.
- `funcs : name → (DefId, body_start, body_end, params)` map
  built during a one-shot index pass.
- `forVarStack` — for nested for loops, the current substitution
  for each loop variable.

## 5. Algorithms

### Pass 1 — index functions and for-loops.

Walk ops; record `phonon.def` markers and their
matching `phonon.end_def`. Skip subsequent passes' work on
those op ranges.

### Pass 2 — emit.

A single forward walk with manual cursor `i`:

```
i = 0
while i < numOps:
  op = ops[i]
  switch op.kind:
    spinor.*:    copy verbatim, threading the value map
    phonon.def:  skip ahead to matching phonon.end_def (do not emit)
    phonon.call: inline the body of the named def, substituting
                 args→params in the value map; advance i++
    phonon.for:  read lo, hi (compile-time int constants); for
                 each iteration value v:
                   forVarStack.push(v)
                   recursively emit ops[i+1 .. body_end-1]
                   forVarStack.pop()
                 advance i past phonon.end_for
    phonon.if:   read predicate-producing op (the cmp); if the
                 chip supports feedforward, emit a
                 `spinor.barrier` (semantic placeholder for
                 "this block is conditional on bit X==Y") followed
                 by the body lowered. If the chip does not
                 support feedforward, AND the body's writes are
                 to qubits already measured (W4 use-after-measure),
                 reject with a diagnostic. Otherwise lower the
                 body unconditionally (the cond evaluates at
                 simulation/runtime; correctness depends on the
                 emitter handling the wrapping). For Phase B's
                 M4, "flatten" means: emit the then-body
                 in-line; the M9 emitter is responsible for
                 wrapping it in QASM3 `if (...) { }` syntax.
                 Phase B M4 chooses the simpler approach: emit
                 the bodies of both branches in-line (which is
                 valid only when both branches are no-ops to
                 the qubit state OR the program has been
                 designed to be correct under both branches —
                 typical of teleportation's classical
                 corrections). The lowering emits a
                 `spinor.barrier` between branches as a marker.
    phonon.const_int / phonon.const_angle / phonon.binop /
    phonon.cmp / phonon.assign:
                 evaluate symbolically (when statically
                 foldable) and update `valueMap`. Otherwise
                 emit nothing (these are not Spinor ops).
    phonon.return / phonon.while:
                 not emitted at top level. Inside a function
                 body, return is the last thing seen during
                 inlining and produces values bound at the call
                 site. While loops require a separate pass; M4
                 ships unsupported-while diagnostics.
```

### Pass 3 — verify

Confirm the output module contains only `spinor.*` ops. If not,
emit a diagnostic listing the offending phonon op kinds; this is
a self-check against algorithm bugs.

## 6. Test matrix

| Test name | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M4_lower.bell_pair_func` | golden | `bell_pair_func.phn` | output module byte-equal to `goldens/bell_pair_func.spnir`. |
| `M4_lower.qft_loop_unrolls` | unit | `qft_loop.phn` | output has exactly 4 `spinor.h` ops. |
| `M4_lower.teleportation_w4` | integration | `teleportation.phn` (target ibm_heron_r2) | lowering succeeds; output uses no Phonon ops. |
| `M4_lower.bell_passthrough` | integration | `spinor/tests/corpus/bell.spn` parsed as Phonon | lowered module is structurally equivalent to the original. |
| `M4_lower.no_phonon_ops_in_output` | property | every Phonon corpus | every op in lowered output has `isSpinorKind == true`. |
| `M4_lower.lowered_passes_phase_a_verify` | integration | every Phonon corpus | output passes `spinor::verify::verify(m, target, diag)`. |

## 7. Cookbook example

`bell_pair_func.phn` (M4 input):

```
target generic
qubit q[4]
bit c[4]

def bell_pair(qubit a, qubit b) {
  h a
  cx a, b
}

bell_pair(q[0], q[1])
bell_pair(q[2], q[3])
c = measure q
```

After lowering (M4 output — flat Spinor):

```
spinor.module @main attributes {target = "generic"} {
  %q0 = spinor.alloc_qubit : !spinor.qubit
  %q1 = spinor.alloc_qubit : !spinor.qubit
  %q2 = spinor.alloc_qubit : !spinor.qubit
  %q3 = spinor.alloc_qubit : !spinor.qubit
  %c0 = spinor.alloc_bit  : !spinor.bit
  %c1 = spinor.alloc_bit  : !spinor.bit
  %c2 = spinor.alloc_bit  : !spinor.bit
  %c3 = spinor.alloc_bit  : !spinor.bit
  %q0_1 = spinor.h %q0 : !spinor.qubit
  %q0_2, %q1_1 = spinor.cx %q0_1, %q1 : !spinor.qubit, !spinor.qubit
  %q2_1 = spinor.h %q2 : !spinor.qubit
  %q2_2, %q3_1 = spinor.cx %q2_1, %q3 : !spinor.qubit, !spinor.qubit
  %m0 = spinor.measure %q0_2 : !spinor.bit
  %m1 = spinor.measure %q1_1 : !spinor.bit
  %m2 = spinor.measure %q2_2 : !spinor.bit
  %m3 = spinor.measure %q3_1 : !spinor.bit
}
```

## 8. Pitfalls

- **Recursive functions.** Phase B forbids recursion; lowering
  detects a self-call via a small DFS-on-emit and emits an error.
- **Loop bound resolution.** If a `for` bound depends on a
  classical variable that wasn't statically folded, the lowering
  fails with a clear error. The parser already enforces this, but
  the lowering double-checks.
- **Conditional emission.** The Phase B M4 simple model emits the
  body of an `if` unconditionally (because the test fixtures all
  use `if` for classical corrections that are no-ops when the
  classical condition is false — Pauli corrections in
  teleportation). The M9 emitter is responsible for wrapping in
  `if`-syntax when targeting QASM3 with feedforward. Decision D9
  records this scope choice.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M4`.
- [ ] Phase A tests still green.
- [ ] Lowered output of every Phonon corpus parses through
      Phase A's verify and resource estimator.
- [ ] `phaseB_progress.md` entry appended; D9 recorded in
      `phaseB_decisions.md`.

## 10. Open questions

None.
