---
title: Vision
---

# Vision

Heisenberg is on a path to be the **first quantum compiler stack
that treats every chip as a first-class target** — and to do it in
a way that does not lock you into one vendor's runtime, one
language, or one cloud.

The shape we are building toward, over the next twelve months:

- **Write a quantum program once, run it on any chip.** Today that
  works for the gate-model world (27 chips across 8 vendors, plus
  4 cassette-only adapters waiting for vendor endpoints).
  Tomorrow it covers analog Rydberg, photonic, and annealing
  hardware — each with its own sibling language that compiles down
  to the same compiler engine.
- **Honest cost control before the cloud bill.** Every job carries
  a real `ResourceEstimate`. The platform refuses to send a job to
  a provider if it would blow the user's budget. No silent
  re-transpilation, no vendor surprise.
- **Three SDKs, one engine.** Python, C++, and TypeScript all call
  into the same C++ compiler — there is exactly one source of truth
  for compilation. RULE 3 turns into a guarantee, not a slogan.
- **Open by default.** Apache-2.0, signed releases on PyPI / npm /
  GitHub Releases, public roadmap, public unsupported-chips ledger
  ("here is exactly the piece of data we are missing"). Nothing
  proprietary except the working names.

## The next 12 months

Three tracks of work, in priority order:

1. **Chip coverage.** Close the
   [unsupported-chips ledger](internals/chips_unsupported.md) by
   adding the missing decomposer recipes (one Spinor patch unblocks
   a dozen chips at once). Add the four cassette-only adapters to
   live mode as each vendor publishes their REST URL.
2. **A second computational model.** Land an analog Rydberg sibling
   language — same engine, same launcher, same docs site — so
   QuEra Aquila and Pasqal Fresnel become first-class targets. The
   design is already in
   [the future plan](internals/futureplan.md); the implementation is
   2-3 weeks of focused work.
3. **Production polish.** A typed `@heisenberg/sdk` on npm, signed
   binaries on every tag, real PyPI publishing, and the
   `heisenberg run` launcher running cleanly on Linux + macOS +
   Windows.

## Where this is heading further out

Two horizons we are not committing to dates on:

- **Photonic and annealing** sibling languages — same architecture
  as the analog Rydberg track but for Xanadu / ORCA (photonic CV)
  and D-Wave (annealing). Each is a clean 3-4 weeks of work but
  the demand is smaller; we follow the customers.
- **Auto-synthesis.** A research summit, deliberately scoped out
  of the compiler. The right home is a separate product on top of
  the finished stack — we will not bolt it onto Photon. The
  rationale lives in
  [the future plan](internals/futureplan.md).

## What you can rely on

- The seven [critical rules](internals/seven_rules.md) bind every
  pull request. Optimisation does not move into Spinor. The provider
  layer never silently rewrites your program. The compiler stays a
  single C++ engine.
- We pin every third-party version explicitly and re-verify upstream
  before bumping (RULE 4). Nothing floats.
- The [unsupported-chips ledger](internals/chips_unsupported.md) is
  the honest list. If a chip is not on either the registry or the
  ledger, we have not heard of it yet.

## Read next

- [The full future plan](internals/futureplan.md) — landscape,
  per-track deep-dives, decision matrix.
- [Current implementation plan](plan.md) — what we are doing right
  now.
- [Progress so far](progress.md) — what has shipped to date.
- [Architecture](internals/architecture.md) — how the four layers
  fit together.

---

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
