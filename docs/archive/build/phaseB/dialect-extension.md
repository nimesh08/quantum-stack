# Phonon dialect — relationship to the Spinor dialect

The Phonon dialect is an **extension** of the Spinor dialect, not
a separate one. The relationship is documented here so reviewers
can verify the containment property mechanically.

## Reuse vs. add

The Phonon dialect inherits the entire Spinor surface:

- All Spinor types: `!spinor.qubit`, `!spinor.bit`.
- All Spinor ops: `alloc_qubit`, `alloc_bit`, every standard and
  native gate, `measure`, `reset`, `barrier`.
- The Spinor module attribute `target = "..."`.

It then adds:

- Two classical types: `!phonon.int` and `!phonon.angle`. Both
  are non-linear (copy/discard freely).
- A function type `!phonon.func<...>` carried as an attribute on
  function-definition ops. (Phase B keeps function bodies as
  contiguous op sequences inside the parent module rather than
  introducing nested regions, to keep the in-tree backend
  simple. The MLIR-backed bridge maps these to `func.func` when
  it builds.)
- Eight control-flow / scoping / classical ops, all in the
  `phonon` namespace:

  | Op | Meaning |
  | --- | --- |
  | `phonon.const_int` | Materialise an integer constant. |
  | `phonon.const_angle` | Materialise an angle constant (radians). |
  | `phonon.binop` | Classical binary expression (`+ - * /`). |
  | `phonon.cmp` | Classical comparison (`== != < >`). |
  | `phonon.if` | Conditional block; consumes a `!spinor.bit` or `!phonon.int` predicate. |
  | `phonon.for` | Counted loop with compile-time bounds. |
  | `phonon.while` | Conditional loop; lowers to bounded unrolling when the iteration count is statically known, otherwise rejected at lowering. |
  | `phonon.def` / `phonon.call` / `phonon.return` | Function definition, call, and return. |

- An `assign` form is purely surface-syntax sugar: a Phonon
  source-level `i = i + 1` becomes a fresh `phonon.binop`
  result that supersedes the symbol-table binding for `i` (no
  IR mutation; classical scalars are non-linear so we may
  redefine the binding freely).

## Interleaving

A Phonon module can mix Spinor ops and Phonon ops freely at the
top level. The lowering pass walks the module top-down,
expanding control-flow / function ops into their
straight-line equivalents and producing a module that contains
**only** Spinor ops, character-equivalent to a hand-written
`.spn` source. M4's tests assert this property mechanically.

## Why this layout

- **Containment is provable.** A Phonon parser that reuses the
  Spinor lexer + grammar productions accepts every legal `.spn`
  file unchanged.
- **Lowering is mechanical.** With Phonon ops as a strict
  superset of Spinor ops, the lowering pass only has to
  *remove* (rewrite away) Phonon ops; it never has to invent
  new Spinor ops.
- **Linear typing rests on the existing SSA.** The
  value-semantics that make Spinor's M3 verifier work
  (operands consumed, results fresh) is exactly what makes
  Phonon's linear type system mechanical: each `!spinor.qubit`
  value has a single producer and must have a single consumer.

## Backend symmetry with Spinor

Phase A introduced a layered backend: an in-tree backend that
builds everywhere, and an MLIR-backed bridge that builds when
LLVM/MLIR 22.1.7 is present (decision D4). Phonon mirrors this:

- The **in-tree** Phonon dialect lives in
  [`phonon/dialect/include/phonon/dialect/Phonon.h`](../../../phonon/dialect/include/phonon/dialect/Phonon.h)
  and its companion `lib/`. It depends only on Spinor's in-tree
  dialect. CI exercises this path on every push.
- The **TableGen** sources (`phonon/dialect/td/PhononOps.td`,
  `PhononTypes.td`, `PhononDialect.td`) are committed in tree
  as the canonical declarations and consumed by the MLIR
  bridge when `QSTACK_HAVE_LLVM` is ON.

This keeps Rule 1 honest (Phase A green throughout) and Rule 3
honest (the production engine is C++ on MLIR/LLVM, with the
in-tree backend kept exclusively for development and CI on
hosts without LLVM).
