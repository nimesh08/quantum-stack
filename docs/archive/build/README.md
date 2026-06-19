# docs/build/

Each phase writes its build trail here. There are three files per phase
(plus a top-level decisions log shared across phases). The agent fills
these in as it works — they are how a future reader (human or agent)
reconstructs how the phase actually got built.

## Files (per phase)

For each phase A, B, C, D, the agent maintains three files:

- **`<phase>-progress.md`** — a running log. One dated entry per work
  session: what was attempted, what landed, what is next. Append-only.
  Example: `phase-a-progress.md`.

- **`<phase>-usage.md`** — the user-facing usage guide for whatever the
  phase produced (CLI flags, library API, examples). Updated as the
  surface area grows. The reader of this file should be able to drive the
  phase's artifact without reading the source.

- **`<phase>-decisions.md`** — the design decisions made during the phase
  that diverge from, refine, or extend the spec. Each entry: the
  decision, the alternatives considered, why this one won, and the link
  back to the spec section it touches. Push-back allowed only where the
  design is wrong or impractical (per the handoff brief).

## Top-level

- **`decisions.md`** — cross-phase decisions (anything affecting more than
  one phase, e.g. version pins, monorepo boundaries, CI policy). Each
  entry uses the same format as the per-phase decisions file.

- **`versions.md`** — the human-readable companion to
  `cmake/Versions.cmake`: what was pinned, on what date, why, and what
  the next bump trigger is.

## Layout

```
docs/build/
├── README.md            (this file)
├── decisions.md         (cross-phase)
├── versions.md          (companion to cmake/Versions.cmake)
├── phase-a-progress.md  (Spinor)
├── phase-a-usage.md
├── phase-a-decisions.md
├── phase-b-progress.md  (Phonon)
├── phase-b-usage.md
├── phase-b-decisions.md
├── phase-c-progress.md  (Photon)
├── phase-c-usage.md
├── phase-c-decisions.md
├── phase-d-progress.md  (Platform)
├── phase-d-usage.md
└── phase-d-decisions.md
```

Stub files for Phase A are created up front; B/C/D stubs are created as
those phases open.

## Conventions

- **Dated entries.** Every entry begins with an ISO-8601 date
  (`## 2026-06-16`) so the order is unambiguous.
- **Append, don't rewrite.** Progress files are journals. If something
  was wrong and is now corrected, write the correction as a new entry
  rather than mutating the old one — the trail matters.
- **One source of truth per fact.** Version numbers live in
  `cmake/Versions.cmake`; this folder explains the *why* and links back.
