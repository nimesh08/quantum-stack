# Build journal

Append-only log of how Heisenberg was built, milestone by milestone.
For full per-phase progress files see the
[archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive/build).
This page is a curated, one-line-per-event summary of the major
landings, in reverse chronological order.

## 2026 — Open-source release

- **0.5.0** — Docker removed. Single-command launcher
  (`pip install heisenberg && heisenberg run`). SQLite default,
  Postgres opt-in. Full author attribution. Three-language
  documentation rewrite. Apache-2.0 LICENSE. PyPI + npm publishing.

## 2026-06 — Steps 1+2 of the chip-coverage roadmap

- 18 new chip YAMLs. Total chips supported: 27 (up from 4). IBM
  Heron r2/r3, Brisbane, Sherbrooke, Osprey, Nighthawk r1, Torrino;
  Quantinuum H1-1, H2-1, Helios; IonQ Tempo, Forte, Forte
  Enterprise, Aria-1, Harmony, Aria-proto; Rigetti Ankaa-2, Ankaa-3,
  Ankaa-9Q-3; IQM Garnet, Emerald; OQC Toshiko; AQT Pine.
- 4 cassette-only adapters: QCI Aqumen, Anyon Yukon, TII Falcon,
  Alice and Bob Boson 4.
- `FUTUREPLAN.md` published at the repo root. The
  [unsupported-chips ledger](chips_unsupported.md) is the public
  list of what we cannot ship and exactly what would unblock each
  row.

## 2026-06 — Phase D platform

- FastAPI 0.137.1 service, SQLAlchemy 2.0, Alembic, APScheduler,
  Postgres 17.10 (now optional with SQLite default), React 19.2 +
  Monaco SPA. 107 tests.
- Cost control on the way in (`cost.check_budget`); calibration
  refresh on the way back (`calibration.main.refresh_one`).

## 2026-05 — Phase C: Photon

- OO language + `photon.lib`. Three frontends — `.pho`, the
  `@photon.kernel` Python decorator, and a Clang LibTooling C++
  ingester — all converging on the same C++ engine via `nanobind`.
  13 tests across the three frontends.

## 2026-05 — Phase B: Phonon

- Structured IR with linear types (no-cloning enforced at the type
  level). Optimizer pipeline: cancellation, rotation merging, ZX
  simplification, scheduling. 17 tests.

## 2026-04 — Phase A: Spinor

- C++ compiler engine. MLIR-style dialect, recursive-descent parser,
  placement, SABRE routing, KAK + Euler-ZYZ decomposition,
  OpenQASM 3 / QIR / Quil emitters. IBM / AWS / Azure submission
  adapters in cassette mode. 116 tests.

---

The full per-phase journals live in the
[archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive/build):
`phaseA_progress.md` through `phaseD_progress.md`. They use
phase- and milestone-level vocabulary that does not surface in the
live docs; this page is the user-facing summary.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
