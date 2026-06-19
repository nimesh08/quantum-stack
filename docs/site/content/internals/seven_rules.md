# The seven critical rules

The constitutional invariants of the project. Every PR is reviewed
against them; if a change breaks any rule, it is reworked or
rejected.

## RULE 1 — Build bottom-up

The order is **Spinor → Phonon → Photon → Platform**. A finished
lower layer is a real, testable artefact the next layer depends on.
This is what makes it possible to fix a Phase B bug without breaking
Phase D, and what makes it possible to add a chip at the YAML level
without touching the compiler at all.

In practice: do not start a feature in Photon that requires a
Phonon change without landing the Phonon change first.

## RULE 2 — Optimization lives in Phonon, never in Spinor

Spinor specialises a program to one chip. It places, routes,
decomposes, and emits. It does **not** rewrite gate sequences for
fewer gates or shallower depth — that is Phonon's job, run once
above all chips so every back-end benefits.

If a circuit-shrinking optimiser appears in Spinor, that is an
architectural error: stop the change, push the rewrite up to Phonon,
and ship the right thing instead.

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

## RULE 5 — Submit to providers in verbatim / pass-through mode only

When Heisenberg hands a chip-locked artefact to IBM, AWS Braket,
Azure Quantum, QCI, Anyon, TII, or Alice and Bob, the provider
runs it **as written**. No silent re-transpilation, no backend-side
optimisation that would change the semantics of the program the user
asked for.

Concretely: every provider adapter passes the appropriate
`skip_transpilation=True` / `#pragma braket verbatim` /
QIR-Adaptive-pragma / equivalent flag to the vendor SDK. If a
vendor cannot honour verbatim mode, the chip lands on the
[unsupported-chips ledger](chips_unsupported.md) until they can.

## RULE 6 — Phase E (auto-synthesis) is out of scope

Auto-synthesis — the dream of *user describes a problem, system
invents the circuit* — sits **above** Photon, not inside any of the
four layers. It is a planner / search problem that *uses* a
compiler, not a compiler. The right move is to treat it as a
standalone product on top of the finished stack; the
[future plan](futureplan.md) explains why.

## RULE 7 — *Photon*, *Phonon*, *Spinor* are working names

Run a trademark search before any public release uses these names
unmodified. Until that search clears, the published Python /
TypeScript packages live under the `heisenberg-*` namespace
(`heisenberg`, `heisenberg-photon`, `heisenberg-spinor-submit`, ...)
so we never paint ourselves into a corner.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
