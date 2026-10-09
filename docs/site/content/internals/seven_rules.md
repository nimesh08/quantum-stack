# The seven critical rules

The constitutional invariants of the project. Every PR is reviewed
against them; if a change breaks any rule, it is reworked or
rejected.

## RULE 1 — Build bottom-up

The order is **Spinor → Phonon → Photon → Platform**. A finished
lower layer is a real, testable artefact the next layer depends on.
This is what makes it possible to fix a Phonon bug without breaking
the platform, and what makes it possible to add a chip at the YAML
level without touching the compiler at all.

In practice: do not start a feature in Photon that requires a
Phonon change without landing the Phonon change first.

## RULE 2 — Optimize at the layer that has the required information

Logical simplification operates on Phonon operations before physical layout.
Spinor owns native synthesis, placement, routing, native gate optimization and
resource scheduling. Both preserve measurements, reset, classical control,
phase and readout mappings. Exact transformations are the default; any future
approximate synthesis requires an explicit error budget. Provider SDKs do not
replace either compiler stage.

## RULE 3 — One C++ engine, one source of truth

There is exactly one compiler. The Python and TypeScript layers do
not reimplement compilation — they call into the C++ engine through
a `nanobind` binding (`photon._engine`) and shell out to the
`spinorc` binary for the parts the binding does not yet expose.

This rule is what stops the project from drifting into three
separate compilers in three languages, each subtly different.

## RULE 4 — Re-verify and pin every version before coding

Every third-party version (LLVM, Eigen, FastAPI, React, ...) is
pinned in exactly one place — `cmake/Versions.cmake` for C++,
each `pyproject.toml` for Python, `package.json` for the playground —
and re-verified upstream before being bumped. The verified date is
recorded next to the pin.

This is why the project does not break on a quiet upstream release:
nothing floats.

## RULE 5 — Own compilation and record mandatory provider processing

Adapters serialize already-native programs, authenticate, submit and retrieve
results. They must not call SDK transpilers or silently substitute another
device. Use documented native/verbatim controls where supported. Mandatory
service processing, such as QCS translation or QAT lowering, is allowed and
recorded in the job receipt. Never invent a bypass option a provider does not
document. Missing or incompatible device contracts block live submission.

## RULE 6 — Auto-synthesis is out of scope

Auto-synthesis — the dream of *user describes a problem, system
invents the circuit* — sits **above** Photon, not inside any of the
four layers. It is a planner / search problem that *uses* a
compiler, not a compiler. The right move is to treat it as a
standalone product on top of the finished stack; the
[future plan](futureplan.md) explains why.

Exact synthesis of an existing unitary into a target's native gates is part
of the compiler and is covered by Rule 2.

## RULE 7 — *Photon*, *Phonon*, *Spinor* are working names

Run a trademark search before any public release uses these names
unmodified. Until that search clears, the published Python /
TypeScript packages live under the `heisenberg-*` namespace
(`heisenberg`, `heisenberg-photon`, `heisenberg-spinor-submit`, ...)
so we never paint ourselves into a corner.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
