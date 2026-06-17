# Platform — Phase D

The product layer that turns the Photon · Phonon · Spinor compiler
into something a person can use: a FastAPI **job service**, a nightly
**calibration refresh**, a React + Monaco **playground**, and a Docker
**deployment**.

> **Documentation:** <https://nimesh08.github.io/quantum-stack/> —
> landing, quickstart, full REST + Python + TypeScript API reference,
> operations runbook, decisions log.

> **Status:** Phase D in progress. The C++ engine (Phases A–C) is
> complete and tested; this folder calls into it via the `photon._engine`
> nanobind binding. We do not rebuild compilation here.

## Subfolders

| Folder         | Purpose                                                |
|----------------|--------------------------------------------------------|
| `jobsvc/`      | FastAPI service + workers + queue.                     |
| `calibration/` | Nightly refresh job (APScheduler).                     |
| `playground/`  | React 19.2 + Monaco SPA.                               |
| `deploy/`      | Dockerfiles + docker-compose.yml.                      |

## Two quantum-specific seams (everything else is conventional)

1. **Cost control** — before queueing, the `ResourceEstimate` from the
   compiler is multiplied by `chip.pricing.per_shot_usd` and compared
   to the user's `Budget`. Over-budget → 402, **before** spending.
2. **Nightly calibration refresh** — APScheduler hits each provider,
   writes `~/.cache/spinor/calibration/<chip>.json` (the path the chip
   YAMLs already declare), the compiler's placement pass picks it up.

## Critical rules that bind this phase

- **RULE 1** — Phase D builds on Phases A–C; no edits to those folders.
- **RULE 5** — Submission is verbatim (pass-through). Always.
- **RULE 6** — Phase E (auto-synthesis) is out of scope.

## Read first

- [End-of-phase platform guide](../docs/build/phaseD_platform_guide.md)
- [Decisions](../docs/build/phaseD_decisions.md)
- [Build journal](../docs/build/phaseD_progress.md)
- [Per-milestone specs](../docs/build/phaseD/README.md)
- [Future plan + chip-coverage roadmap](../FUTUREPLAN.md)
- [Unsupported chips ledger](../docs/site/content/chips_unsupported.md)
