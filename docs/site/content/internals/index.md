# Internals

Honest, design-level material for people who want to understand how
Heisenberg is built — not just how to use it.

This section is for contributors and the merely curious. It contains
the architecture, the constitutional rules that bind every layer, the
decisions log (why we picked SQLite over per-job state files, why
Phonon owns the optimizer, why the provider layer is verbatim only),
the build journal, the public roadmap, and the unsupported-chips
ledger.

## Pages

- [Architecture](architecture.md) — the four-layer compiler, the
  platform that wraps it, and the data flow from a `.spn` source to
  a histogram in the browser.
- [Seven critical rules](seven_rules.md) — the constitutional
  invariants. Every PR is reviewed against them.
- [Decisions log](decisions.md) — every deviation from the original
  engineering deep-dives, with the reasoning.
- [Build journal](build_journal.md) — append-only log of how the project
  was built, milestone by milestone. Linked to the
  [archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive)
  for the per-phase progress files.
- [Future plan](futureplan.md) — the chip-coverage roadmap and the
  three-DSL extension plan.
- [Unsupported chips](chips_unsupported.md) — the honest ledger.
- [Glossary](glossary.md) — every acronym, in one place.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
