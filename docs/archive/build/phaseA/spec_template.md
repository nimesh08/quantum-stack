# Per-milestone Spec Template

Copy this file when starting a new milestone spec. Every milestone
spec under `docs/build/phaseA/` uses these ten sections in this
order. The point of the template is that a reviewer can answer
"is this milestone safe to merge?" by checking each section in
turn.

---

## 1. Goal & spec section

One paragraph: what this milestone delivers, and the exact
section in the [Spinor Engineering Deep-Dive][deep-dive] this
implements. If it implements no Deep-Dive section (e.g. a
testing harness), say so and link the relevant general spec.

[deep-dive]: ../../../docs/spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

Precise contracts.

- **Consumes:** what file types / IR shapes / data structures
  arrive at this milestone.
- **Produces:** what the milestone hands to the next milestone
  (artifacts, IR transformations, CLI subcommands, etc.).
- **Invariants on output:** anything that must be true of every
  output (e.g. "every two-qubit gate is on a connected pair").

## 3. Deliverables

A bulleted list of every file path created or modified, every
CMake target added, every CLI subcommand or flag introduced. A
reviewer can use this as a checklist while diffing the PR.

## 4. Data structures

Every new C++ type, MLIR op or type, YAML key, or JSON Schema
field. For each: name, fields, units/ranges, owner, lifetime.
This is the "if I had to write it down for a colleague joining
next week" view.

## 5. Algorithms

Pseudo-code or paper citation for any non-trivial logic. The
goal: a reviewer can check correctness without reading code.
Include complexity bounds (time + space) for anything the
optimizer might call repeatedly.

## 6. Test matrix

Literal table — one row per test. **This section is
non-negotiable**; it is the row that prevents "we forgot to
test X."

| ID    | Name | Type | Inputs | Expected output | CI gate |
| ----- | ---- | ---- | ------ | --------------- | ------- |

`Type` ∈ {unit, integration, property, fuzz, regression,
golden, e2e}.

`CI gate` is the named CTest label and the GitHub Actions job
that runs it.

## 7. Cookbook example

One worked example end-to-end. Show the input, every
intermediate form, and the final output, with a sentence
explaining what changed at each step. The example must be
readable by all three audiences (quantum-only, compiler-only,
neither — see the on-ramps in
[M0_overview.md](M0_overview.md)).

## 8. Pitfalls

A "watch out for X" list. Every footgun this milestone could
hit. Each item references the test in §6 that catches it. New
pitfalls may be added during code review; the spec is updated
in place.

## 9. Definition of Done

Checkbox list. Milestone is not complete until every box ticks.
Defaults that apply to every milestone (copy as the first
boxes):

- [ ] Spec landed and reviewed (this file).
- [ ] All tests in §6 implemented and green locally.
- [ ] All tests in §6 green in CI.
- [ ] Coverage on changed files ≥ 85%.
- [ ] Test matrix
      ([test-matrix.md](test-matrix.md)) updated with new rows.
- [ ] Glossary ([glossary.md](glossary.md)) updated with any
      new terms.
- [ ] Progress journal
      ([phaseA_progress.md](../phaseA_progress.md)) appended
      with a dated entry.
- [ ] Decisions log
      ([phaseA_decisions.md](../phaseA_decisions.md)) appended
      with any deviations from the Deep-Dive.

## 10. Open questions

Anything that needs human input before code starts. If empty,
the milestone is unblocked.
