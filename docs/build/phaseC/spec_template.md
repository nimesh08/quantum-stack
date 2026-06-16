# Phase C spec template

Every `Mxx_*.md` under [`phaseC/`](README.md) uses the ten sections
below in this order. Identical shape to Phase A and Phase B.

---

## 1. Goal & spec section

One paragraph. The goal of the milestone, plus the exact section of
the Photon & Frontends Deep-Dive (Parts 1–2) being implemented.

## 2. Inputs / outputs

What the milestone consumes (file types, IR shapes, build-config
schemas) and what it produces (Phonon IR, Python module, CLI tool,
binding artifact). Be precise; reviewers must be able to verify
shape contracts without reading code.

## 3. Deliverables

Every file path created or modified, every CMake target, every CLI
subcommand or flag added. A mechanical checklist.

## 4. Data structures

All new C++ types, Python classes, AST/visitor classes, build-config
schemas with field-by-field constraints.

## 5. Algorithms

Pseudo-code or paper citation for any non-trivial logic (lib.grover
expansion, decorator AST walk, Clang RecursiveASTVisitor mapping).

## 6. Test matrix

A literal table — one row per test — with name, type
(unit / integration / property / fuzz / regression / golden / e2e),
inputs, expected output, CI gate. Rolled up into
[`test-matrix.md`](test-matrix.md).

## 7. Cookbook example

One worked example end-to-end. M1: the Grover kernel; M4: the
8-line Bell decorator example; M5: the C++ Bell kernel. Readable
by all three audiences.

## 8. Pitfalls

A "watch out for X" list — every footgun and the test that catches
it. Examples: Python `inspect.getsource` on REPL-defined
functions, AST line-number drift across CPython point releases,
Clang's `CompilerInstance` invocation flags, nanobind's GIL
semantics, `.pho` lexer hash-comment vs `#pragma`.

## 9. Definition of Done

Checkbox list. Milestone is not complete until every box is
ticked.

## 10. Open questions

Anything that needs human input before code starts; if empty, the
milestone is unblocked.
