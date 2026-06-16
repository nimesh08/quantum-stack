# Glossary — Phase A

Every quantum, compiler, and MLIR term used anywhere in Phase A,
defined once here. If you find yourself using a term not on this
list, add it.

Organised by topic, not alphabetised, so a reader picking up an
unfamiliar area can read top-to-bottom of one section.

---

## Quantum concepts

**Bit.** A classical storage cell; always exactly 0 or 1.

**Qubit.** A quantum storage cell. Until measured, it can be in
a *superposition* — a blend of 0 and 1 simultaneously. Modeled
in the Spinor IR as the type `!spinor.qubit`.

**Superposition.** The state where a qubit is neither 0 nor 1
but a probabilistic blend of both. Becomes definite only on
measurement.

**Measurement.** The act of reading a qubit. Collapses any
superposition to a definite 0 or 1, with probabilities given by
the prior blend. Modeled by the `spinor.measure` op.

**Shot.** One end-to-end run of a quantum program. Because
measurement is probabilistic, every program runs many shots
(typically 1,000–10,000) and the answer is the *histogram* of
results.

**Gate.** One quantum instruction. Acts on one or more qubits.
Always reversible (no information is lost). Modeled by the
gate ops in the Spinor dialect (`spinor.h`, `spinor.cx`,
`spinor.rz`, etc.).

**Standard gate set.** The vendor-neutral gates Spinor allows
under `target generic`: `h x y z s sdg t tdg rx ry rz cx cz
swap`. These are not what real chips execute; they are what
humans write.

**Native gate set.** The gates a real chip physically executes.
Each chip has its own set: IBM Heron exposes `ecr rz sx x`;
IonQ Forte exposes `gpi gpi2 ms` (or `rzz` interactions);
Quantinuum Helios exposes its own. The registry tells the
compiler which gates are native to which chip.

**Coupling map.** The undirected graph of which physical
qubits can talk to which on a chip. Superconducting chips
(IBM, Rigetti) have sparse, fixed coupling maps (e.g. heavy-hex
honeycomb). Trapped-ion chips (IonQ, Quantinuum) are
all-to-all. Stored in the registry per chip.

**SWAP.** A two-qubit gate that exchanges the states of its
operands. Used to "walk" a qubit through the chip's coupling
map until it sits next to a qubit it must interact with. Each
SWAP costs three native two-qubit gates and injects error.

**Routing.** The pass that inserts the fewest SWAPs to make
every two-qubit gate executable on the chip's coupling map.

**Placement / initial mapping.** The function from virtual
qubit indices (the names the program uses) to physical qubit
indices on the chip. A good placement keeps interacting qubits
close.

**Decomposition.** Rewriting a standard gate into an
equivalent sequence of the chip's native gates. Spinor uses
*exact* decomposition — the rewrite is a mathematical
identity, not an approximation.

**Stabilizer / Clifford circuit.** A restricted class of
quantum circuits — those built only from `h s cx` (plus
`measure`, `reset`) — that can be simulated efficiently on a
classical computer (no exponential blow-up). Stim handles this
class.

**Statevector simulator.** A classical simulator that tracks
the entire quantum state as a vector of $2^n$ complex numbers.
Exact, but only practical for small $n$ (≤24-ish on a laptop).
Spinor builds its own in C++ on Eigen.

**Verbatim mode (a.k.a. pass-through mode).** A submission
mode every major provider supports, in which the provider's
own compiler is disabled and the circuit runs exactly as
submitted. Required for our quality claims to be ours, not
theirs.

**KAK decomposition.** The theorem (Cartan KAK; Vatan-Williams
PRA 69, 032315) that any two-qubit unitary can be written as
≤3 native two-qubit entanglers interleaved with single-qubit
rotations. M6 uses this as a closed-form recipe.

**Euler ZYZ decomposition.** The theorem that any single-qubit
unitary can be written as `Rz(α) · Ry(β) · Rz(γ)`. M6 uses this
as a closed-form recipe.

**Solovay–Kitaev theorem.** Held in reserve. Relevant only if a
chip's native set is *discrete* (no continuous rotations); none
of our launch chips need it.

---

## Compiler concepts

**Source / source language.** What a human writes. For Phase A,
that's the `.spn` text format described in the grammar (Deep-Dive
Part 1 §3).

**Front end.** The compiler stage that reads source text and
produces the compiler's internal form, having checked the source
is legal. Does not optimize. Phase A's front end = parser +
verifier + IR builder.

**Lexer / tokenizer.** Groups raw characters into tokens
(`target`, `qubit`, `[`, `123`, `cx`, etc.).

**Parser.** Matches a token stream against the grammar,
producing a tree (the *AST*).

**AST (abstract syntax tree).** Tree-shaped, mirrors the
grammar. Fed to the IR builder.

**IR (intermediate representation).** The compiler's internal
form. Stays the same as it is rewritten by passes. Spinor's IR
is an MLIR dialect.

**Pass.** A transformation over the IR. Each pass takes an IR
module and rewrites it. Phase A's three core passes are
placement, routing, and decomposition.

**Verifier.** A pass that checks legality but doesn't transform.
M3 implements W1–W6.

**SSA (static single assignment).** The discipline that every
value is defined exactly once. MLIR works this way. In our IR,
every gate consumes a qubit value and produces a new qubit
value, so each qubit is "used linearly" — using it twice is a
detectable error. This is the foundation no-cloning rests on.

**Lowering.** Transforming IR from a higher-level dialect to a
lower-level one. Our compiler lowers `target generic` Spinor IR
to `target <device>` Spinor IR. Phonon will lower its IR to
Spinor IR.

**Recursive-descent parser.** A parser written by hand as one
function per grammar rule, each calling the others. Used in M7.

**EBNF.** Extended Backus–Naur Form (ISO/IEC 14977). The
notation we use for the grammar.

---

## MLIR concepts

**Dialect.** A named, self-contained vocabulary of MLIR
operations and types. We define `spinor` (M1) and later `phonon`
(Phase B). Precedent: NVIDIA's CUDA-Q defines its own `quake`
dialect.

**Op (operation).** One node in the MLIR IR. Has operands
(input values), results (output values), and attributes
(compile-time constants like rotation angles). Defined via
TableGen.

**TableGen.** LLVM's domain-specific language for declaring
ops, types, and patterns. Files end in `.td`; the build
generates C++ headers from them.

**Type.** The kind of value an op consumes/produces. We define
`!spinor.qubit` and `!spinor.bit`.

**Attribute.** A compile-time constant attached to an op.
Rotation angles are attributes (`F64Attr:$angle`); the target
chip is a module-level attribute (`spinor.target = "ibm_heron_r2"`).

**Region / block.** Structural concepts MLIR uses to model
control flow. Phase A IR has no regions; each function-equivalent
is a single block of ops. Phonon will introduce regions for
control flow.

**`assemblyFormat`.** A TableGen field specifying how an op
prints / parses in MLIR's textual form. Critical for round-trip
testing in M1.

**`verify`.** A C++ method (or TableGen-generated) that checks
op-level invariants beyond what the type system can enforce.

**Pass manager.** MLIR's machinery for running a pipeline of
passes. Our `spinorc compile` builds a pass manager and runs
placement → routing → decomposition → cleanup on it.

**Pattern rewriter.** MLIR's framework for matching IR
patterns and rewriting them. Used by decomposition (M6) and
cleanup (M6).

---

## Provider / format terms

**OpenQASM 3.** A text format for quantum programs, current
spec 3.1. Looks like assembly. Used by IBM (Qiskit) and AWS
(Braket).

**`#pragma braket verbatim box`.** Braket's verbatim-mode
syntax. The `box { ... }` body must use native gates, physical
qubits (written `$0`, `$1`, ...), and every two-qubit gate
must sit on a connected pair.

**QIR (Quantum Intermediate Representation).** A format for
quantum programs expressed as LLVM bitcode. Two profiles
relevant to us: **Base** (no measurement-dependent branching)
and **Adaptive** (allows it). Used by Azure Quantum and
Quantinuum.

**Quil.** Rigetti's text format for quantum programs.
arXiv:1608.03355. Narrow in reach (Rigetti devices) but cheap
to support.

---

## Spinor-specific terms

**`target generic`.** The portable contract: standard gates,
virtual qubits, no connectivity check. Default starting point
for a human-written program.

**`target <device-id>`.** The hardware contract: that chip's
native gates only, physical qubit indices, two-qubit gates only
on connected pairs. The form `spinorc` produces.

**`spinorc`.** The Phase A command-line compiler. Subcommands:
`parse`, `verify`, `compile`, `check`, `emit`, `submit`,
`registry`. See the [user guide](../phaseA_spinor_guide.md).

**Spinor IR / Spinor-HW.** The MLIR module after the front end
(generic) and after Part-2 passes (hardware), respectively.

**Check lane.** The simulator + equivalence check + resource
estimator that runs after the compile passes and before
submission. M8 builds this.

**Two-contract model.** The design pattern of one grammar with
two strictness levels selected by the first line. Spinor's
single most important design decision; the verifier branches
on it.

**The six legality rules (W1–W6).** What the verifier
enforces beyond the grammar. See M3 spec for full detail.

**The interaction graph.** A graph built from the IR where
nodes are virtual qubits and edges are weighted by how often
two qubits appear together in a two-qubit gate. Placement uses
it.

**The distance matrix.** All-pairs shortest-paths over the
chip's coupling map. Routing uses it for SABRE scoring. We
implement it ourselves in C++ (no rustworkx in the engine).
