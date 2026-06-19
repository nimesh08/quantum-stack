---
title: Current plan
---

# Current implementation plan

This page is the user-facing version of what we are building right
now. For the long-horizon roadmap see
[Vision](vision.md); for what has already shipped see
[Progress](progress.md).

## Goal

Ship Heisenberg as a proper open-source project — single-command
install, three-language documentation, signed releases on PyPI and
npm — without changing the architecture or losing any of the work
that already landed.

## Locked-in decisions

- **One install command for everyone.** `pip install heisenberg
  && heisenberg run` brings up the FastAPI service, the queue
  worker, and the calibration scheduler in one process. Production
  deployments split them out as systemd units.
- **SQLite by default.** The data directory at
  `~/.local/share/heisenberg/` holds a SQLite database, the
  calibration cache, the JWT signing key, and the PID file. Postgres
  is opt-in via `JOBSVC_DATABASE_URL`.
- **Static playground.** The React + Monaco SPA is built once into
  `jobsvc/static/` and served by the FastAPI service at `/`. nginx
  is gone.
- **No Docker.** The compose file, the Dockerfiles, and the nginx
  reverse proxy were removed. The native launcher does the same job
  with less rope.
- **Apache-2.0 license, full author attribution.** Every source
  package, every doc page, the README, and the site footer name
  Nimesh Cheedella. Git history was rewritten so every commit shows
  the same author.
- **Phase-talk banned in user-facing docs.** Internal milestone
  vocabulary (alphabetic phase letters, numeric milestone tags)
  appears only in the build journal, the changelog, and the
  `docs/archive/` tree.
- **Three SDKs, one engine.** Python (mkdocstrings), C++ (Doxygen),
  TypeScript (TypeDoc) — all reference the same C++ engine through
  `nanobind` or `spinorc`.
- **Signed, OIDC-published releases.** PyPI via trusted publishing,
  npm with provenance, C++ binaries with cosign + SBOM via syft.

## What is shipping in 0.5.0

- The `heisenberg` Python package with `init / seed / run / stop /
  playground build / version` subcommands.
- A `jobsvc` service that serves the playground at `/` and uses
  SQLite by default.
- A `@heisenberg/sdk` npm package with a typed REST client and
  React Query hooks.
- A documentation site reorganised into Vision, Plan, Progress,
  Languages, SDKs, Operations, Internals, Reference, FAQ,
  Changelog, Authors. Three auto-generated reference trees (Python
  via mkdocstrings, C++ via Doxygen, TypeScript via TypeDoc, REST
  via Redoc).
- Three production systemd units at
  [`platform/systemd/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/systemd).
- Apache-2.0 LICENSE; AUTHORS, CONTRIBUTING, CODE_OF_CONDUCT,
  SECURITY, dependabot, CODEOWNERS; a strict docs linter
  ([`scripts/check_docs.py`](https://github.com/nimesh08/quantum-stack/blob/main/scripts/check_docs.py))
  and a cursor scrubber
  ([`scripts/check_no_cursor.sh`](https://github.com/nimesh08/quantum-stack/blob/main/scripts/check_no_cursor.sh))
  wired into CI.
- A signed release pipeline
  ([`release.yml`](https://github.com/nimesh08/quantum-stack/blob/main/.github/workflows/release.yml))
  that triggers on `v*.*.*` tags.

## What is left for 0.5.0 to be live

Two external steps that need credentials we do not commit:

1. **Force-push the rewritten history to `origin/main`.** The new
   author-clean history exists locally, with a backup branch and
   tag (`pre-rewrite-2026-06-19`) preserved. Until the push lands,
   the public GitHub repo still shows the pre-rewrite state.
2. **Configure trusted publishers and tag `v0.5.0`.** PyPI / TestPyPI
   trusted publishing and npm's OIDC provenance need one-time
   project-level configuration in their respective web UIs. After
   that, pushing the tag fires
   [`release.yml`](https://github.com/nimesh08/quantum-stack/blob/main/.github/workflows/release.yml)
   end-to-end.

## Next, after 0.5.0

Top of the queue is **closing the unsupported-chips ledger**. One
patch in
[`spinor/passes/Decomposition.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/passes/Decomposition.cpp)
adds the missing CZ-from-CX decomposer recipe, which unblocks 12
chips simultaneously. The 4 cassette-only adapters convert to live
mode one PR at a time as each vendor publishes their REST URL.

After that, the analog Rydberg sibling language; see
[Vision](vision.md) and
[the future plan](internals/futureplan.md).

## How decisions are made

- Every change is reviewed against the
  [seven critical rules](internals/seven_rules.md).
- Substantive rationale lives in the
  [decisions log](internals/decisions.md).
- The build journal in
  [`internals/build_journal.md`](internals/build_journal.md) is
  append-only — one entry per release, written for contributors
  rather than users.

---

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
