# Phase D (Platform) — progress journal

Append-only build journal for Phase D. New entries on top.

For per-milestone engineering specs see [`phaseD/`](phaseD/README.md).
For deviations from the Platform Services Deep-Dive see
[`phaseD_decisions.md`](phaseD_decisions.md). The end-of-phase user
guide is [`phaseD_platform_guide.md`](phaseD_platform_guide.md).

---

## 2026-06-16 — M0: bootstrap

What landed:

- `platform/{jobsvc,calibration,playground,deploy}/` skeleton.
- `platform/jobsvc/pyproject.toml` pinning FastAPI **0.137.1**,
  SQLAlchemy 2.0, asyncpg, alembic, apscheduler, pydantic 2.7,
  python-jose, passlib, structlog, prometheus-client, httpx.
- `platform/calibration/pyproject.toml` pinning apscheduler, httpx.
- `platform/playground/package.json` pinning React **19.2.7**,
  `@monaco-editor/react ^4.7.0`, monaco-editor 0.52, Vite 6,
  TypeScript 5.6, TanStack Query 5, Zustand 5, Recharts 2,
  Vitest 2, Playwright 1.48.
- `.github/workflows/phase-d-ci.yml` with five jobs:
  jobsvc-unit, jobsvc-integration (Postgres 17.10 service),
  calibration, playground-unit, compose-smoke.
- `docs/build/phaseD/{README.md, M0_overview.md}` opened;
  `docs/build/phaseD_decisions.md` opened with D1, D2.

Tests: 0/0 (CI wiring proven).

## 2026-06-16 — M1: job model + storage

What landed:

- `jobsvc.models`: `User`, `ApiKey`, `Budget`, `Job`, `Result`,
  `CalibrationSnapshot`, `AuditLog`. Cross-dialect `GUID` and JSONB
  helpers so the same models work against Postgres 17 and SQLite
  (the unit-test substrate).
- `jobsvc.models.LEGAL_TRANSITIONS` (7 edges) + `Job.transition()`
  with timestamp stamping (`queued_at`, `started_at`, `finished_at`)
  and a typed `IllegalTransitionError`.
- `jobsvc.audit.record()` — appends `AuditLog` rows in the caller's
  session/transaction (no implicit IO).
- `jobsvc.config.Settings` + `jobsvc.db` (async engine, sessionmaker,
  `session_scope` context manager and `get_session` FastAPI dep).
- `alembic.ini` + `alembic/env.py` + `alembic/versions/0001_init.py`
  authoring the Postgres schema explicitly.

Tests: **26/26 unit passing** (`tests/unit/`).

## 2026-06-16 — M3: cost control (seam 1)

What landed:

- `jobsvc.cost.dollar_cost` (Decimal, 6 dp) +
  `jobsvc.cost.check_budget` (pure, easy to unit-test).
- `jobsvc.cost.recent_spend_for_user` — sums `dollar_cost` over the
  caller's spending-state jobs in the current UTC day and month.
- `routers.jobs.create_job` consults the budget between compile and
  queue. Over-budget → state=Rejected, structured 402 reply.
- 12 unit cases + 6 integration cases for the seam.

Tests: **67/67 passing** (37 unit + 30 integration).

## 2026-06-16 — M4: workers + providers

What landed:

- `jobsvc.queue` — Postgres-as-queue with `FOR UPDATE SKIP LOCKED`
  + `NOTIFY` (D1). SQLite path for unit tests.
- `jobsvc.providers` — `submit_to_provider(engine, chip, shots)`,
  routing on `chip.provider` to (a) the in-process local simulator
  or (b) the Phase A `spinor_submit.submit` adapter (verbatim mode,
  Rule 5).
- `jobsvc.worker.process_one` — claim, recompile, submit, store
  histogram, transition to Completed/Failed; with Prometheus
  metrics for queue_depth, lease_expirations, provider_latency,
  errors_total{kind}.
- `tests/integration/test_worker.py` — 7 cases including
  end-to-end Bell, lease expiry, provider/our error classification.
- `tests/regression/test_cassette_submit.py` — full
  `spinor_submit` cassette path against ibm/bell.json.

Tests: **75/75 passing**.

## 2026-06-16 — M5: calibration refresh (seam 2)

What landed:

- `platform/calibration/` package — APScheduler-driven refresher.
- Providers: `fixture` (always works), `ibm` (qiskit-ibm-runtime),
  `aws` and `azure` as v1 stubs (D5).
- `writer.write_atomic` — `tmpfile + os.replace`.
- `diff.diff` — relative-change detector.
- `main.run_once` (cron mode) and `main.run` (daemon).
- 19 tests across atomic write, providers, scheduler, run_once.

Tests: **19/19 calibration**, **75/75 jobsvc** = 94/94 cumulative.

## 2026-06-16 — M6: Playground

What landed:

- React 19.2.7 + Vite 6 + TypeScript 5.6 SPA at
  `platform/playground/`.
- Three Monaco Monarch grammars (spinor/phonon/photon) lifted from
  the Phase A/B/C lexer keyword lists.
- Pages: Playground (split-pane), Jobs, Settings, Login. Auth via
  Zustand store + localStorage; bearer token attached by the
  fetch wrapper; 401 clears the token; 402 detail surfaced as a
  friendly banner (over-budget rejection).
- Histogram via Recharts BarChart.
- Vitest unit suite + jsdom + ResizeObserver polyfill: 8 tests
  green. Production `vite build` succeeds. `tsc -b` clean.
- Playwright e2e (Bell-through-stack) ships gated to compose mode.

Tests: **8/8 playground unit**, 75/75 jobsvc, 19/19 calibration =
102/102 cumulative.

## 2026-06-16 — M7: operations

What landed:

- `jobsvc.seed` CLI: `python -m jobsvc.seed email pwd [user|admin]`.
- `tests/integration/test_isolation.py` — five M7-specific cases:
  two-users-isolated, third user can't cancel, admin can cancel
  anyone's job, /metrics surface, x-request-id round-trip.
- `platform/deploy/`: Dockerfile.jobsvc + .worker + .calibration +
  .playground (multi-stage), docker-compose.yml (Postgres 17.10 +
  jobsvc + 2× worker + scheduler + playground via nginx), nginx.conf,
  .env.example, run.sh.
- Per-milestone specs `M0`–`M7` and `glossary.md` finalised.

Tests: **80/80 jobsvc** + **19/19 calibration** + **8/8 playground**
= **107/107 cumulative**.

## 2026-06-16 — Phase D complete

What landed:

- `docs/build/phaseD_platform_guide.md` — end-of-phase user-facing
  guide. Three on-ramps (from quantum / from web / from neither),
  five-minute quickstart, full API reference (curl + JSON examples),
  Playground walkthrough, operations (logs/metrics/health/migrations
  /calibration), the two quantum-specific seams, pinned versions,
  where-to-put-new-code, troubleshooting, definition-of-done.
- `docs/build/phaseD/glossary.md` — platform-side jargon.
- `docs/build/phaseD/README.md` index updated.
- All eight milestone specs (`M0` through `M7`) finalised under
  `docs/build/phaseD/`.

Tests: **107/107** = 80 jobsvc + 19 calibration + 8 playground.
Production builds: vite green; tsc -b clean; alembic upgrade
applies; docker-compose YAML parses.

End of Phase D. Phases A–D together are a complete, usable product.
Phase E (auto-synthesis) stays deliberately out of scope per Rule 6.

---

## 2026-06-16 — M2: FastAPI surface + engine call

What landed:

- `jobsvc.engine.compile_program` — wrapper around the Phase C
  nanobind binding `photon._engine.compile_phonon`, with a
  deterministic stub fallback when the engine isn't built on the
  host. Returns a uniform `EngineResult { ok, error, target,
  estimate, spinor_qasm3 }`.
- `jobsvc.registry` — reads `spinor/registry/chips/*.yaml`,
  exposes `ChipInfo` with `pricing_per_shot_usd` (used by M3 cost
  check) plus a synthetic `generic` target.
- `jobsvc.schemas` — Pydantic 2 request/response models for every
  endpoint.
- Routers: `health`, `login`, `targets`, `users` (incl. budget +
  api-keys + admin), `jobs` (POST/GET/list/DELETE).
- Auth scaffolding: bcrypt passwords, HS256 JWT pair (access +
  refresh), `X-API-Key` header path, `current_user` /
  `require_admin` FastAPI deps.
- structlog JSON logger with request-id middleware; Prometheus
  registry exposed at `/metrics`.
- `jobsvc.main:app` puts it all together; 13 OpenAPI paths live.

Tests: **49/49 passing** (26 unit + 23 integration).

What's next: M3 — cost control.

---

