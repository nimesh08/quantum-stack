# Phase B (Phonon) — glossary

Every quantum / compiler / Phonon-internal term used in the
Phase B docs and code, defined once. If you find a term in the
docs that is not here, that is a documentation bug — file or fix.

## Quantum

- **Bell state.** The two-qubit entangled state $(|00\rangle + |11\rangle)/\sqrt{2}$,
  prepared by `H` then `CX`.
- **Clifford circuit.** A circuit composed of Clifford gates
  (`H`, `S`, `CX`, etc.). Simulable in polynomial time.
- **Decoherence.** Loss of quantum information to the environment.
  Ticks the clock on every quantum program.
- **Entanglement.** A correlation between qubits that cannot be
  described by independent classical states.
- **Gate.** A unitary operation on one or more qubits.
- **GHZ state.** The N-qubit generalisation of the Bell state.
- **No-cloning theorem.** The physical law that no operator can
  copy an unknown qubit. Phonon makes attempts to violate it a
  compile-time error.
- **Measurement.** The operation that collapses a qubit's state
  to a classical bit.
- **Native gate set.** The (small) set of gates a chip implements
  directly. Decomposition is the act of expressing a circuit in
  this set.
- **Reset.** Re-prepare a measured or active qubit in the |0⟩
  state, releasing the previous use.
- **Teleportation.** A three-qubit protocol that uses
  entanglement and feed-forward to transmit a qubit's state.
  Phase B's marquee use of mid-circuit measurement +
  classical-controlled gates.

## Compiler

- **AST (abstract syntax tree).** Tree-structured intermediate
  form produced by the parser before IR construction.
- **CSE (common-subexpression elimination).** Optimisation;
  re-use the result of an earlier identical computation.
- **Dialect (MLIR).** A namespaced collection of types/ops/attrs
  inside MLIR's framework. Phonon's dialect *extends* Spinor's.
- **IR (intermediate representation).** The data structure the
  compiler manipulates between source and machine code.
  Phonon's IR is its dialect.
- **Linear type.** A type whose values must be used exactly once
  (no duplication, no silent drop). Used here to enforce
  no-cloning.
- **Lowering.** A pass that translates a higher-level dialect
  into a lower-level one. Phonon → Spinor lowering = inline +
  unroll + flatten.
- **Pass.** A program transformation. The optimizer is a
  sequence of passes.
- **Pattern rewrite.** A pass form: match a syntactic pattern in
  the IR and replace it with another. MLIR ships a framework
  (`PatternRewriter`); the in-tree backend has a tiny
  hand-written analogue for Phase B's own passes.
- **SSA (static single assignment).** Each value is assigned
  exactly once. Spinor's IR is already SSA: every gate consumes
  its input qubit value and produces a fresh result. Linear
  typing rests on this.

## Phonon-internal

- **Affine vs linear.** A *linear* value must be used exactly
  once. An *affine* value may be used at most once (i.e. it can
  be silently dropped). Phonon is linear-strict by default and
  emits a warning, not an error, on implicit discard. See
  [`M3_linear_types.md`](M3_linear_types.md).
- **Built pass.** An optimizer pass we write end-to-end:
  cancellation, rotation merging, commutation reordering,
  scheduling.
- **Borrowed pass.** An optimizer pass whose core algorithm
  comes from an external library, behind an owned C++ interface
  in `phonon/optimizer/borrowed/`. Phase B borrows tweedledum
  (classical-logic synthesis) and PyZX/quizx (ZX simplification).
- **Containment.** The property that every legal Spinor program
  is a legal Phonon program, character for character. Verified
  by parsing the entire Phase A corpus through the Phonon
  parser and checking the produced IR matches.
- **IRouter, ISynthesizer, IZxSimplifier.** The three borrow
  interfaces. Each has a `NullImpl` (identity, no-op) that ships
  in-tree so the pipeline runs in CI without external deps.
- **Linear-type lifecycle.** A qubit's life is a chain of values:
  alloc → gate → gate → … → measure → reset (optional). The
  type checker rejects any branching of this chain.
- **Lowering bundle.** The three lowering passes
  (function-inline, loop-unroll, conditional-flatten) run as a
  fixed-order group; tests assert the bundle's output is pure
  Spinor.
- **Optimize-once principle.** Phonon's optimizer runs exactly
  once, before lowering, above all chips. Spinor never optimises.
  See R2 in [`M0_overview.md`](M0_overview.md).
- **Phn file.** A Phonon source file. Identical to `.spn` files
  for the Spinor subset; adds control-flow / function syntax for
  the Phonon-only constructs.
- **Swap policy.** Build whatever determines result quality
  (cancellation/merging/reorder/schedule); borrow whatever
  encodes deep, well-solved math (synthesis, ZX) behind owned
  interfaces so it is replaceable. Qiskit never enters the
  optimize path.
- **W4 relaxation.** Phase A registry flag (`mid_circuit_measure`)
  that lets a chip support gate-after-measure. The Phonon type
  checker honours this exactly as Spinor's M3 verifier does.
