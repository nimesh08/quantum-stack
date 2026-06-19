# Decisions log

Curated, human-readable list of every architectural decision that
deviates from the original engineering deep-dives. Each entry has a
short title, the decision in one sentence, and a pointer at the
file or commit where it landed.

The full per-phase decision tables are kept in the
[archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive/build) —
that is the verbose, milestone-language version. This page is the
short version, in chronological order.

## D1 — Default database is SQLite, not Postgres

Postgres is correct for production; SQLite is correct for laptops
and CI. The launcher defaults to
`sqlite+aiosqlite:///~/.local/share/heisenberg/jobsvc.db`; pass
`--postgres URL` to override. The schema is dialect-aware (UUIDs as
`CHAR(36)` on SQLite, native `uuid` on Postgres; `JSONB` becomes
`JSON`).

Lives in:
[`platform/jobsvc/src/jobsvc/_alembic/versions/0001_init.py`](https://github.com/nimesh08/quantum-stack/blob/main/platform/jobsvc/src/jobsvc/_alembic/versions/0001_init.py),
[`platform/jobsvc/src/jobsvc/db.py`](https://github.com/nimesh08/quantum-stack/blob/main/platform/jobsvc/src/jobsvc/db.py).

## D2 — Single-command launch via `heisenberg run`

Docker is gone. The launcher boots `jobsvc` + worker + scheduler in
one process for laptops, splits them into systemd units for
production. The browser opens automatically in `--dev` mode.

Lives in: [`platform/launcher/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/launcher).

## D3 — Provider layer stays Python

The C++ engine ships a `LocalProvider` for the in-process simulator.
Real cloud submission (IBM, Braket, Azure, QCI, Anyon, TII, Alice
and Bob) lives in
[`spinor/submit/python/spinor_submit/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/submit/python/spinor_submit) — one file, one
`_live_<vendor>` per provider, one `submit()` dispatch.

Reason: each vendor SDK is Python; calling them from C++ would mean
bridging seven independent SDKs through `nanobind`. Not worth it.

## D4 — Add a chip = write one YAML, no compiler change

[`spinor/registry/chips/<id>.yaml`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips)
is the single source of truth: native gate set, topology, calibration
source, pricing. The compiler reads it; nothing else needs to know.

Reason: that is the difference between supporting 27 chips and
supporting 4. The full coverage table is in the
[future plan](futureplan.md).

## D5 — Chips we cannot ship today are on a public ledger

If a chip's data is not publicly verified, we do **not** silently
ship a half-correct YAML. The chip lands on
[unsupported chips](chips_unsupported.md) with the exact piece of
data we are missing and what would unblock it. One PR per row.

## D6 — Documentation is human-first

Phase- and milestone-language is banned from the live site. Every
page begins with a one-paragraph lede and a runnable example.
Internal build prose lives in the
[archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive),
not in user-facing nav.

A docs linter
([`scripts/check_docs.py`](https://github.com/nimesh08/quantum-stack/blob/main/scripts/check_docs.py))
enforces this in CI.

## D7 — `mkdocs-material` 9.7.x is in maintenance through Nov 2026

We pin `9.7.6` and revisit when the upstream maintenance window
closes. The
[mkdocs-material maintenance notice](https://squidfunk.github.io/mkdocs-material/blog/2026/02/announcing-the-end-of-life-of-9-x/)
is the authoritative source.

## D8 — `LLVM/Clang/MLIR` 22.1.x line is the C++ floor

Pinned at `22.1.8` (final 22.1.x). Next bump trigger: 23.1.0 once
stabilised (23.x branches 2026-07-14, GA 2026-08-25). See
[`cmake/Versions.cmake`](https://github.com/nimesh08/quantum-stack/blob/main/cmake/Versions.cmake).

## D9 — `nanobind` 2.12.0, Python 3.13 target / 3.12 floor

The Photon decorator rejects older interpreters at import time so
the failure mode is one line of clear error rather than a cryptic
ABI miss.

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
