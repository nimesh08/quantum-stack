# M5 — Built optimizer passes

## 1. Goal & spec section

Implement Phonon's "build whatever determines result quality"
optimizer passes. Implements **Phonon Deep-Dive Part 3 §§7.1–7.3,
7.6**.

Four passes, all in-tree, all meaning-preserving:

1. **Gate cancellation** — adjacent inverse-pair gates are
   removed (`H H`, `X X`, `Sdg S`, `Tdg T`, `CX CX`, `CZ CZ`,
   `Sxdg Sx`).
2. **Rotation merging** — adjacent same-axis rotations fuse
   (`Rz(a); Rz(b)` → `Rz(a+b)`); zero-fold drops `Rz(0)`.
3. **Commutation reordering** — slide commuting neighbours past
   each other to expose cancellation/merging opportunities (e.g.
   `Rz(a); H` does NOT commute, but `Rz(a); X` commutes around
   commuting partners; this Phase B pass implements a small
   commutation table and applies bubble-sort within barrier
   segments).
4. **Scheduling** — orders operations to minimise total depth
   without changing semantic order on shared qubits. Implemented
   as the canonical "as-soon-as-possible" greedy scheduler.

These passes operate on the **Phonon dialect**, not the Spinor
dialect — that is the whole point of the optimize-once principle.

## 2. Inputs / outputs

- Each pass takes a `phonon::dialect::Module` and rewrites it in
  place. Returns a `Stats` struct (gates removed, depth before/
  after, etc.) for diagnostics and benchmarks.
- Passes are exposed via `phonon::optimizer::run(...)` which
  applies them in a sensible order (cancellation, merging,
  reorder + cancellation+merging again, schedule).
- Each pass is also independently callable for testing.

## 3. Deliverables

- `phonon/optimizer/include/phonon/optimizer/Optimizer.h` —
  public API.
- `phonon/optimizer/include/phonon/optimizer/BuiltPasses.h` —
  individual pass entry points.
- `phonon/optimizer/lib/Cancellation.cpp`,
  `RotationMerging.cpp`, `CommutationReorder.cpp`,
  `Scheduling.cpp`, `Pipeline.cpp`.
- `phonon/optimizer/CMakeLists.txt`.
- Tests in `phonon/tests/m5/`.

## 4. Data structures

```cpp
namespace phonon::optimizer {

struct Stats {
  std::size_t gatesRemoved = 0;
  std::size_t rotationsMerged = 0;
  std::size_t reorderingsApplied = 0;
  std::size_t depthBefore = 0;
  std::size_t depthAfter = 0;
};

Stats cancelInversePairs(dialect::Module& m);
Stats mergeRotations(dialect::Module& m);
Stats commuteAndReduce(dialect::Module& m);
Stats schedule(dialect::Module& m);

// Run the canonical built-only pipeline.
Stats runBuiltPipeline(dialect::Module& m);

}  // namespace phonon::optimizer
```

A small per-qubit "last writer" map is the workhorse data
structure. The passes walk ops in order, look at the previous op
producing the same qubit, decide if the pair cancels/merges,
rewrite the IR.

## 5. Algorithms

### Cancellation

For each `spinor.*` 1q gate or 2q gate, check whether the
immediately preceding op on the same qubit(s) is its inverse:

| Gate | Inverse |
| --- | --- |
| `h` | `h` |
| `x` | `x` |
| `y` | `y` |
| `z` | `z` |
| `s` | `sdg` |
| `t` | `tdg` |
| `cx` (a,b) | `cx` (a,b) |
| `cz` (a,b) | `cz` (a,b) |
| `sx` | `sxdg` |

Implementation: walk ops; maintain a `lastOpOn[qubit] = OpId`
map. When a candidate cancellation is detected, mark **both**
ops as removed (replace with a no-op kind we tag in metadata).
After the walk, copy live ops to a fresh module.

### Rotation merging

Adjacent `Rz(a); Rz(b)` on the same qubit (no other op between
them on that qubit) merges to `Rz(a+b)`. If `a+b == 0` (mod 2π),
the merged op is dropped. Same for `Rx`, `Ry`. The merged op
inherits the location of the first.

### Commutation reorder

For Phase B M5, the commutation table:

| A | B | Commute? |
| --- | --- | --- |
| `Rz(a)` | `Rz(b)` | yes (same axis) |
| `Rz(a)` | `cx` ctrl | yes (Rz on control) |
| `Rx(a)` | `Rx(b)` | yes |
| `Z` | `cx` ctrl | yes |
| `X` | `cx` tgt  | yes |
| any | barrier | no |
| any | reset / measure | no |

Pass: bubble-sort within each segment between barriers/measures,
swapping neighbours whose relative order doesn't matter.
Reorderings expose new cancellation/merge opportunities; the
pipeline runs cancellation+merging again after reorder.

### Scheduling

Compute, for each op, a "ready time" = `max(ready_time[q]) + 1`
over its qubit operands. Then sort op indices by ready time
(stable). The output module's op order matches the sort. Depth
= max ready time.

## 6. Test matrix

| Test | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M5_cancel.h_h_cancels` | unit | `H q[0]; H q[0]` | output empty (after alloc). |
| `M5_cancel.cx_cx_cancels` | unit | `CX a,b; CX a,b` | output empty. |
| `M5_cancel.no_false_cancel_when_other_op_between` | unit | `H q; X q; H q` | nothing cancels. |
| `M5_merge.rz_rz_fuses` | unit | `Rz(0.5); Rz(-0.5)` | output drops both. |
| `M5_merge.rz_zero_drops` | unit | `Rz(0)` | output drops it. |
| `M5_reorder.rz_z_pair_reorders_then_merges` | property | `Z; Rz(a); Z` | merges to one `Rz(a + 2π)` mod 2π → could drop. |
| `M5_schedule.depth_for_independent_qubits` | property | `H q[0]; H q[1]` | depth = 1. |
| `M5_pipeline.bell_unchanged` | integration | bell module | semantically equivalent. |
| `M5_pipeline.runs_pipeline_no_phonon_loss` | integration | bell_pair_func parsed | pipeline succeeds; gate counts ≤ original. |

Equivalence is checked structurally for Phase B (no Spinor sim
on the Phonon dialect yet); the M7 e2e test runs the lowered
output through Phase A's M8 sim for full equivalence.

## 7. Cookbook example

```
target generic
qubit q[1]
h q[0]
h q[0]      ; cancels with the previous H
rz(pi/2) q[0]
rz(-pi/2) q[0]   ; merges to Rz(0), drops
```

After `runBuiltPipeline`: only the `alloc_qubit` remains.

## 8. Pitfalls

- **Operand identity.** Cancellation must compare qubit
  *operands*, not just gate kinds. `H %q0; H %q1` does NOT
  cancel.
- **Two-qubit gate symmetry.** `cx a, b` and `cx a, b` on the
  same control/target cancel; `cx a, b` and `cx b, a` do **not**
  (different gate).
- **Barriers and measure boundaries.** Optimizer must not move
  ops past a `barrier` or `measure` on the same qubit.
- **Numerical merging.** Rotation merge sums angles in radians;
  near-zero check uses `1e-12` tolerance.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M5`.
- [ ] Pipeline run on every Phonon corpus does not regress gate
      counts.
- [ ] Phase A tests still green.
- [ ] `phaseB_progress.md` entry appended.

## 10. Open questions

None.
