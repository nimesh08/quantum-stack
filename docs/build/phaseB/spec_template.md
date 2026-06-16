# Phase B spec template

Every `Mxx_*.md` under [`phaseB/`](README.md) uses the ten sections
below in this order. The shape is identical to Phase A's
`spec_template.md`; read both side-by-side if you are migrating.

---

## 1. Goal & spec section

One paragraph. The goal of the milestone, plus the exact section
of the Phonon Engineering Deep-Dive (Parts 1–3) being implemented.

## 2. Inputs / outputs

What the milestone consumes and what it must produce. Be precise:
file types, IR invariants, calling conventions, error contract.

## 3. Deliverables

Every file path created or modified, every CMake target, every CLI
subcommand or flag added. A mechanical checklist; reviewers should
be able to verify "the files in §3 exist" without reading code.

## 4. Data structures

All new C++ types, MLIR ops/types, JSON/YAML schemas, with
field-by-field descriptions and constraints.

## 5. Algorithms

Pseudo-code or paper citation for any non-trivial logic (loop
unrolling, function inlining, conditional flattening, pattern
rewriting, ZX simplification, etc.). Reviewers should be able to
check correctness without reading the code.

## 6. Test matrix

A literal table — one row per test — with name, type
(unit / integration / property / fuzz / regression / golden / e2e),
inputs, expected output, and CI gate. Updated in
[`test-matrix.md`](test-matrix.md) too.

## 7. Cookbook example

One worked example end-to-end (e.g. for M4: a `bell_pair` function
in Phonon is inlined and a 2-iteration `for` loop is unrolled into
flat Spinor that is byte-equal to the spec's worked example),
readable by all three audiences.

## 8. Pitfalls

A "watch out for X" list — every footgun. Each pitfall references
the test that catches it. Examples: implicit-discard heuristics,
SSA vs. region semantics in MLIR's `scf` dialect, tweedledum's
qubit-numbering convention, PyZX's circuit→graph→circuit
non-uniqueness.

## 9. Definition of Done

Checkbox list. Milestone is not complete until every box is
ticked.

## 10. Open questions

Anything that needs human input before code starts; if empty, the
milestone is unblocked.
