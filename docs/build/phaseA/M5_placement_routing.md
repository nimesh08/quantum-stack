# M5 — Placement + SABRE Routing

## 1. Goal & spec section

The two passes that take a generic-target Spinor IR and assign
its virtual qubits to one chip's physical qubits, then insert
the fewest SWAPs that make every 2-qubit gate executable.
Implements [Spinor Engineering Deep-Dive][deep-dive] Part 2 §3
(placement) and §4 (SABRE routing).

Reference: SABRE (Li, Ding & Xie, ASPLOS 2019,
arXiv:1809.02573).

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:**
  - A `dialect::Module` under `target generic` (well-formed
    by M1 and legal under W5 by M3).
  - A `registry::ChipInfo` (the target chip).
- **Produces:**
  - A new `dialect::Module` under `target <chip-id>` with
    every two-qubit gate operating on a connected pair.
  - An `Annotations` side-table recording the chosen
    initial layout (virtual → physical) and the SWAP
    sequence inserted, for resource-estimator and
    debugging consumption.
- **Invariants:**
  - Every two-qubit op in the output sits on a pair listed
    in `chip.coupling` (or anywhere when `chip.allToAll`).
  - Single-qubit ops are unchanged in semantics.
  - Output is structurally well-formed (M1 verifier passes).
  - The output's contract attribute is the chip id.

## 3. Deliverables

- `spinor/passes/include/spinor/passes/Placement.h`
- `spinor/passes/include/spinor/passes/Routing.h`
- `spinor/passes/include/spinor/passes/CouplingGraph.h` —
  in-tree all-pairs shortest-paths.
- `spinor/passes/lib/Placement.cpp`
- `spinor/passes/lib/Routing.cpp`
- `spinor/passes/lib/CouplingGraph.cpp` — BFS-per-node.
- `spinor/passes/CMakeLists.txt`
- `spinor/tests/m5/CMakeLists.txt`
- `spinor/tests/m5/coupling_test.cpp`
- `spinor/tests/m5/placement_test.cpp`
- `spinor/tests/m5/routing_test.cpp`
- `spinor/tests/m5/property_test.cpp` — randomised "every 2q
  on a wired pair" invariant.
- (Optional) `spinor/tools/sabre_proto/sabre.py` +
  `tests/test_parity.py` — Python+rustworkx reference for
  cross-checking swap counts. We compute swap counts in C++
  and compare against the Python reference for a small
  benchmark suite.

## 4. Data structures

```c++
namespace spinor::passes {

class CouplingGraph {
 public:
  CouplingGraph(std::size_t n, std::vector<std::pair<int,int>> edges,
                bool allToAll);
  std::size_t qubits() const { return n_; }
  bool connected(int a, int b) const;
  // distance d(a,b); returns INT_MAX if disconnected.
  int distance(int a, int b) const;
  // neighbours of u, in ascending order.
  const std::vector<int>& neighbours(int u) const;
 private:
  std::size_t n_;
  bool allToAll_;
  std::vector<std::vector<int>> adj_;
  std::vector<std::vector<int>> dist_;  // n×n
};

struct Layout {
  // virtual_idx -> physical_idx
  std::vector<int> v2p;
  // physical_idx -> virtual_idx (-1 if free)
  std::vector<int> p2v;
};

class Placement {
 public:
  Layout run(const dialect::Module& m, const CouplingGraph& g) const;
};

struct RoutingResult {
  dialect::Module module;          // routed IR
  Layout initialLayout;
  Layout finalLayout;
  std::size_t swapCount = 0;
};

class Routing {
 public:
  RoutingResult run(const dialect::Module& m,
                    const registry::ChipInfo& chip,
                    const CouplingGraph& g,
                    const Layout& initial) const;
};

}  // namespace spinor::passes
```

## 5. Algorithms

### CouplingGraph

- Build adjacency lists from edges (or generate all-to-all on
  demand for small n).
- BFS-per-node populates `dist_`. Time O(n·(n+m)) ≤ O(n²) for
  the chip sizes we care about (≤200 qubits).

### Placement

We use the published SABRE recipe: do a *reverse* SABRE pass
to seed an initial layout, then return that layout. The simple
fallback (used until reverse SABRE is wired) is interaction-graph
greedy assignment:

1. Build the interaction graph: nodes = virtual qubits,
   weights = number of 2q gates between them.
2. Sort virtual qubit pairs by weight, descending.
3. For each (u, v): if both unassigned, place them on a
   connected physical pair near the centre of the chip; if
   one assigned, place the other on a neighbour; ties broken
   by lower index.
4. Any unassigned virtual qubits go to the lowest-index
   unused physical qubits.

This produces a "good enough" starting layout that SABRE then
refines bidirectionally. The reverse-SABRE seed is a Phase-A
acceptable simplification; the placement-quality bar is "every
2q gate routable" and the resource-estimator records the swap
count for inspection.

### SABRE forward pass

Maintain:

- `layout`: current virtual→physical mapping
- `front`: the "front layer" — 2q gates whose predecessors
  on each operand are already executed
- `executed[v]`: per virtual qubit, the next op index

Loop:

1. While there is a gate `g` in `front` whose operand pair is
   already adjacent under `layout`, commit `g` and advance.
2. Otherwise, build `extended` (the set of next-K gates) and
   score every candidate SWAP in the local neighbourhood:

   ```
   score(SWAP s) = (1/|front|) Σ_{g∈front}    d_after(s, g) +
              W * (1/|extended|) Σ_{g∈extended} d_after(s, g)
   ```

   where `d_after(s, g) = dist[layout′(s)[a]][layout′(s)[b]]`.
3. Pick the SWAP with minimum score (ties: lowest physical
   index). Insert it into the output IR, update `layout`.

Constants: `W=0.5` (the paper's default extended-set weight),
`|extended|=20` next gates.

### Bidirectional refinement

- Run forward pass, save final layout.
- Reverse the program, use the final layout as initial,
  run forward pass on the reversed program.
- Repeat once more if needed (the paper notes ≤3 iterations
  is plenty).

For Phase A we ship the *forward* SABRE pass with extended-set
look-ahead. The bidirectional refinement is a refinement loop
on top, gated by a config option (default off, can be enabled
in tests). The Definition-of-Done bar is "every 2q gate on a
connected pair after routing"; M5-T08 turns on bidirectional
and asserts equal-or-better swap count vs forward-only.

### Routing output

Routing rewrites the module:

- Translate virtual qubit indices to physical via `layout`.
- Emit `swap` ops where SABRE chose them; update `layout`.
- Pass single-qubit gates through unchanged.
- Pass measure/reset/barrier through unchanged (they're
  qubit-local).

## 6. Test matrix

| ID    | Name                                    | Type        | Inputs                                    | Expected output                                              | CI gate         |
| ----- | --------------------------------------- | ----------- | ----------------------------------------- | ------------------------------------------------------------ | --------------- |
| M5-T01 | `M5_coupling.linear_distances`         | unit        | linear-4 graph                             | d(0,3)=3, d(0,0)=0, d(1,2)=1                                | `ctest -L M5`   |
| M5-T02 | `M5_coupling.disconnected`             | unit        | two components                             | distance INT_MAX between components                          | `ctest -L M5`   |
| M5-T03 | `M5_coupling.all_to_all`               | unit        | all-to-all 8-qubit                         | d(i,j)=1 for i!=j; d(i,i)=0                                 | `ctest -L M5`   |
| M5-T04 | `M5_placement.bell_on_linear`          | unit        | Bell program on linear-4                  | layout puts q[0],q[1] on adjacent physical                   | `ctest -L M5`   |
| M5-T05 | `M5_placement.ghz_on_linear`           | unit        | GHZ-3 on linear-4                          | every interacting pair on adjacent physicals                 | `ctest -L M5`   |
| M5-T06 | `M5_routing.bell_no_swap`              | integration | Bell on linear-4 with placement           | swapCount = 0                                                | `ctest -L M5`   |
| M5-T07 | `M5_routing.long_range_inserts_swaps`  | integration | cx q[0],q[3] on linear-4                  | swapCount > 0; output every 2q on connected pair             | `ctest -L M5`   |
| M5-T08 | `M5_routing.all_to_all_zero_swaps`     | integration | random 4q circuit on all-to-all chip      | swapCount = 0                                                | `ctest -L M5`   |
| M5-T09 | `M5_routing.invariant_holds`           | property    | 100 random 8q circuits on linear-8        | every routed module has every 2q on a connected pair         | `ctest -L M5`   |
| M5-T10 | `M5_routing.target_attribute_set`      | unit        | any                                        | output module has `target = "<chip-id>"`                     | `ctest -L M5`   |
| M5-T11 | `M5_routing.fourth_chip_round_trip`    | integration | corpus on `ionq_aria_proto`               | YAML-only chip routes via the same code path                 | `ctest -L M5`   |
| M5-T12 | `M5_routing.bidirectional_no_worse`    | integration | benchmark suite                            | bidir swap count ≤ forward-only swap count                   | `ctest -L M5`   |

Cross-check vs rustworkx Python reference (M5-T13) is
implemented as a pytest sub-suite that imports rustworkx,
runs a known-shape SABRE on the same circuits, and asserts our
swap counts are within a small constant factor.

## 7. Cookbook example — `cx q[0], q[3]` on linear-4

Source:

```
target generic
qubit q[4]
cx q[0], q[3]
```

Linear-4 coupling: 0–1–2–3.

Step 1 — Placement: q[0]→0, q[1]→1, q[2]→2, q[3]→3 (default
identity layout, since the only pair (0,3) doesn't fit any
adjacency; SABRE will move).

Step 2 — Routing front layer = {cx q0,q3}. Operands at phys 0
and 3, distance 3. Candidate SWAPs at depth 1 from operand
positions: SWAP(0,1), SWAP(1,2), SWAP(2,3). Scoring:

| SWAP | layout after | d(operands) | score |
| ---- | ------------ | ----------- | ----- |
| (0,1) | q[0]@1, q[3]@3 | 2 | 2 |
| (1,2) | q[0]@0, q[3]@3 (q[1]↔q[2]) | 3 | 3 |
| (2,3) | q[0]@0, q[3]@2 | 2 | 2 |

Tie → lowest index → SWAP(0,1). Now q[0]@1, q[3]@3, distance 2.
Repeat: SWAP(1,2) gets q[0]@2, distance 1. SWAP(2,3) gets
q[0]@3, distance 0 → but that takes q[3] off its physical home.

Algorithm continues with SABRE's scoring; the optimal sequence
is two SWAPs (move q[0] one neighbour at a time, or move q[3]):

```
swap q[0], q[1]    ; now q[0]@1, q[1]@0
swap q[1], q[2]    ; now q[0]@2, q[2]@1  (q[1] still @0)
cx q[0], q[3]      ; q[0]@2 and q[3]@3 are adjacent — done
```

Result: 2 SWAPs, every 2q gate on connected pair, target
attribute = chip id.

## 8. Pitfalls

- **Graph re-use.** `CouplingGraph::distance` is a hot path
  inside the SABRE inner loop; cache the full distance matrix
  once per chip (re-built when a calibration refresh changes
  edges, but constant within one compile).
- **Layout vs lineage.** Routing rewrites *operands* (physical
  index) but the IR's value-id-based lineage from M3 must stay
  intact — the verifier-after-routing check in M5-T09 catches
  drift.
- **SWAP semantics.** `swap a, b` exchanges the qubit values.
  In our IR it's a two-result op; we must thread the swapped
  values through to subsequent operands of those qubits.
- **Tie-breaking determinism.** Multiple SWAPs may share the
  same score. We tie-break by lowest physical index, then
  lowest physical pair (low,high). Without this, tests are
  flaky.
- **Bidirectional and SSA.** Reversing a SSA program means
  reversing op order AND swapping operand/result roles for
  unitary gates (the inverse of `H` is still `H`, but `cx` is
  its own inverse, etc.). For Phase A we keep bidirectional
  refinement scope-tight: it operates on the *physical layout*
  search, not on the IR. The output IR is built once forward.

## 9. Definition of Done

- [ ] Spec landed.
- [ ] All M5-T## tests pass.
- [ ] Routed modules from the corpus all satisfy the
      "every 2q gate on connected pair" invariant.
- [ ] The 4th chip routes via the same code path.
- [ ] Test matrix updated.
- [ ] Glossary updated for any new terms (none expected).
- [ ] Progress journal appended.

## 10. Open questions

None blocking. Bidirectional refinement and rustworkx parity
are both noted as optional/post-Phase-A polish items.
