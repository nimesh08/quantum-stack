# Changelog

All notable changes to the Heisenberg Quantum Stack are documented
in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/).

Author: Nimesh Cheedella.

## [Unreleased]

### Added

- New top-level docs pages
  ([Vision](https://nimesh08.github.io/quantum-stack/vision/),
  [Plan](https://nimesh08.github.io/quantum-stack/plan/),
  [Progress](https://nimesh08.github.io/quantum-stack/progress/))
  and a "Where to look" table near the top of the README so a user
  immediately sees where Heisenberg is going, what we are building
  right now, what has shipped, and where the source lives.

### Changed

- User-facing docs no longer use the historical phase-letter or
  numeric milestone vocabulary. Pages including the FAQ, the seven
  critical rules, the glossary, the unsupported-chips ledger, and
  `FUTUREPLAN.md` were rewritten to use the layered names
  (Spinor / Phonon / Photon / Platform) and named support classes
  (yaml-only / adapter / vendor-blocked / new-language /
  auto-synthesis).
- `FUTUREPLAN.md` gained a plain-English "Vision" section at the
  top.
- `internals/progress.md` renamed to `internals/build_journal.md`
  (with an mkdocs redirect from the old slug).
- `scripts/check_docs.py` now bans historical milestone vocabulary
  everywhere except `internals/build_journal.md`,
  `internals/futureplan.md`, and `changelog.md`.

## [0.5.0] — 2026-06-19

### Added

- Single-command launcher: `pip install heisenberg && heisenberg
  run`. Boots jobsvc + worker + scheduler in one process for
  laptops; `--separate-processes` mode for production servers.
- Native systemd unit files at `platform/systemd/`:
  `heisenberg-jobsvc`, `heisenberg-worker`, `heisenberg-calibration`,
  plus a shared `/etc/heisenberg/heisenberg.env`.
- SQLite as the default database (in `~/.local/share/heisenberg/`);
  Postgres remains opt-in via `JOBSVC_DATABASE_URL`. Alembic
  migrations are now dialect-aware.
- Three-language SDK documentation: Python (mkdocstrings), C++
  (Doxygen), TypeScript (TypeDoc), plus REST (Redoc).
- `internals/` documentation section with architecture, the seven
  critical rules, decisions, build journal, future plan, and
  unsupported chips.
- Doc linter (`scripts/check_docs.py`) that bans phase-talk,
  enforces ledes, and checks links.
- Cursor scrub (`scripts/check_no_cursor.sh`) wired into CI.
- Apache-2.0 `LICENSE`, `AUTHORS.md`, `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, `SECURITY.md`, GitHub
  `ISSUE_TEMPLATE/*`, `PULL_REQUEST_TEMPLATE.md`,
  `dependabot.yml`, `CODEOWNERS`.
- New CI workflows: `lint.yml`, `test-matrix.yml`, `codeql.yml`,
  `docs-lint.yml`. The Python+Node test matrix runs on
  Ubuntu and macOS arm64.

### Changed

- jobsvc serves the built playground SPA at `/`. The default port
  is 8080 (was 8000).
- Documentation rewritten to be human-first: every page begins
  with a one-paragraph lede; phase-talk is restricted to the
  archive and the `internals/` section.
- Old `docs/build/` and `docs/specs/` moved to `docs/archive/`,
  preserved verbatim.
- `mkdocs.yml` reorganised: top-level Languages, SDKs, Operations,
  Internals, Reference; `mkdocs-redirects` maps every old URL to
  its new home.
- Package metadata (`pyproject.toml`, `package.json`,
  `CMakeLists.txt`) now lists Nimesh Cheedella as author and
  declares Apache-2.0.
- Git history rewritten so every commit is authored by Nimesh
  Cheedella.

### Removed

- All Docker artefacts: `Dockerfile.*`, `docker-compose.yml`,
  `nginx.conf`, `run.sh`. The native launcher and systemd units
  replace them.
- The phase-named CI workflows (`phase-a-ci.yml`, `phase-b-ci.yml`,
  `phase-c-ci.yml`, `phase-d-ci.yml`); replaced by `ci.yml`.

### Migration notes

- If you previously ran `docker compose up`, switch to
  `pip install heisenberg && heisenberg run`. The data directory
  default moved from per-container volumes to
  `~/.local/share/heisenberg/`.
- If you previously connected to a Postgres at
  `localhost:5432/jobsvc`, set `JOBSVC_DATABASE_URL` explicitly to
  preserve that path; the new default is SQLite.
- Old documentation URLs continue to work via `mkdocs-redirects`;
  bookmarks should not break.

## Earlier history

The pre-0.5 series predates the open-source release. The
per-phase journals (`phaseA_progress.md` through
`phaseD_progress.md`) are kept verbatim in
[`docs/archive/build/`](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive/build).
