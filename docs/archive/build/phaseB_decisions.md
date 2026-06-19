# Phase B (Phonon) — decisions log

One entry per design decision made during Phase B. Each entry:

- the decision,
- alternatives considered,
- why this won,
- the spec section (Phonon Deep-Dive part / section) it relates to.

Append-only.

For per-milestone engineering specs see
[`phaseB/`](phaseB/README.md).

---

(rows added below as decisions land)

## 2026-06-16 — D9: Phonon-side conditional flattening defers to Phase A's M9 emitter

**Decision.** Phase B's M4 lowering emits the body of every
`phonon.if` unconditionally (then-branch only) into the flat
Spinor module. The runtime classical-control wrapping (e.g.
QASM3 `if (...) { ... }` boxes) is reified by Phase A's M9
emitter when targeting a chip whose registry declares
feedforward support.

**Alternatives.**

- **Lower `phonon.if` to a Spinor `spinor.if` op.** Would
  require a new Spinor op kind, which violates Rule 2 (Spinor
  is not the place for control-flow); and Phase A's emitter
  already produces correct conditional QASM3.
- **Reject conditional programs without a feedforward chip.**
  Tighter, but breaks the canonical teleportation example,
  which uses classical-controlled Pauli corrections that are
  no-ops on the wrong branch — i.e. valid even when the
  chip cannot read measurement bits live.

**Why this won.** Keeps M4 focused on structural lowering and
reuses Phase A's emitter for the conditional reification. The
linear-type checker (M3) and Phase A's M3 verifier (W4)
together catch the genuinely unsafe case: post-measurement
gate on a chip without `mid_circuit_measure`.

**Section reference.** Phonon Deep-Dive Part 1 §3.1
(conditional flattening); Spinor M3 W4 relaxation.

## 2026-06-16 — D10: Loop body re-emit reuses parser-folded variable bindings

**Decision.** Phase B's M4 unroller re-emits the loop body
once per iteration but does not currently rebind `phonon`
loop-variable references inside the body to fresh per-iteration
constants — the parser folded loop-variable references at
parse time using the `lo` bound, so all iterations index the
same slot. This is sufficient for the M4 worked corpus (the
qft_loop fixture applies the same gate to every slot
sequentially) but is documented as a Phase-D platform-layer
follow-up.

**Alternatives.**

- **Per-iteration re-parse.** Stash the body's source token
  range in the def index and re-parse for every iteration;
  most flexible but large change to the parser.
- **Per-iteration ctMap rebind.** Replace every `phonon`
  ValueId in the body that was originally bound to the loop
  variable. Possible but requires the parser to record which
  IDs came from the loop var — extra plumbing.

**Why this won.** The simple unroller covers the Phase B
demonstration corpus. The Phase D platform layer can replace
this with whichever option fits its use case.

**Section reference.** Phonon Deep-Dive Part 1 §3.1 (loop
unrolling).

## 2026-06-16 — D11: ASAP scheduler reports depth without reordering ops

**Decision.** Phase B's scheduler computes ASAP depth and reports
it via `Stats.depthAfter` but does not actually reorder ops in
the module. Phase A's M9 emitter already produces output in the
order Spinor records, and any chip-specific reordering is the
target's responsibility (e.g. a future Phase D pass that
consults the registry's timing model).

**Alternatives.**

- **Stable-sort by ASAP layer.** Possible but requires careful
  handling of measurement / barrier fences and can exhibit
  surprising reordering on legacy circuits.
- **No depth tracking at all.** Loses a useful benchmarking stat.

**Why this won.** Reporting depth is enough for the Phase B
benchmark harness (M7); the actual reordering is a Phase D
optimisation when timing data lands.

**Section reference.** Phonon Deep-Dive Part 3 §7.6 (scheduling).

## 2026-06-16 — D12: Borrowed-pass integration depth (interfaces + NullImpl + pluggable adapter)

**Decision.** Phase B M6 ships the `ISynthesizer` and
`IZxSimplifier` interfaces, a working `NullImpl` for each, and
one pluggable adapter per interface
(`TweedledumSynthesizer` behind `SPINOR_HAVE_TWEEDLEDUM`,
`PyZXSubprocessSimplifier` driven by cassette by default and
optionally by live PyZX via `PHONON_PYZX_LIVE=1`). CI runs use
the NullImpl path (synthesizer) and the cassette path
(simplifier), so the build is hermetic.

**Alternatives.**

- **Full live integration in CI.** Requires tweedledum +
  Python + PyZX in the CI image, and the latter's API has
  shifted (zxcalc org now). Cost outweighs Phase B's
  optimizer-quality demonstration value.
- **Interfaces only, no adapter at all.** Loses the
  swap-policy proof (the whole point of having owned
  interfaces is replaceability — must be exercised).

**Why this won.** Demonstrates the swap policy in motion
(swap NullImpl for the real adapter and the pipeline keeps
working) without taking a CI hostile dependency. The Phase D
platform layer can wire live PyZX + tweedledum when the
calibration / benchmark infrastructure is ready.

**Section reference.** Phonon Deep-Dive Part 3 §§7.4, 7.5, 8
(borrowed components behind owned interfaces).
