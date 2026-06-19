# Phase D (Platform) — progress journal

Append-only build journal for Phase D. New entries on top.

For per-milestone engineering specs see [`phaseD/`](phaseD/README.md).
For deviations from the Platform Services Deep-Dive see
[`phaseD_decisions.md`](phaseD_decisions.md). The end-of-phase user
guide is [`phaseD_platform_guide.md`](phaseD_platform_guide.md).

---

## 2026-06-17 - photonc + phononc CLIs and signed binary distribution

What landed:

- **Three driver binaries**, one per language layer, mirroring
  `gcc cpp -> cc1 -> as -> ld`:
  - [`photonc`](photon/cli/photonc_main.cpp) - top driver. Reads
    `.pho` (in-process via `photon_lang`), `.phonon`, or `.spinor`.
    Subcommands: `compile`, `estimate`, `submit`, `run`, `targets`,
    `providers`, `version`. ~430 LOC.
  - [`phononc`](phonon/cli/phononc_main.cpp) - middle driver. Same
    subcommand set, smaller scope (no `.pho`). ~250 LOC.
  - Existing `spinorc` extended with a `submit` subcommand that
    matches the universal flag schema; legacy
    parse/verify/compile/emit/check/registry stay backward
    compatible.
- **Shared `common/cli` library** at
  [`common/cli/`](common/cli/), single source of truth for:
  - `Flags.{h,cpp}` - declarative flag table, `--help` renderer, no
    secrets in argv (matches the open-cli-collective working-with-
    secrets standard).
  - `Providers.{h,cpp}` - chip-id -> provider lookup that reads the
    chip YAML directly.
  - `Submit.{h,cpp}` - subprocess to `python -m spinor_submit`; the
    vendor SDKs handle their own auth chains.
  - `Manifest.{h,cpp}` - JSON sidecar with `source_sha256` so a
    recipient can audit a portable artifact before submitting.
  - 22 unit tests, all green.
- **Python entry point** at
  [`spinor_submit/__main__.py`](spinor/submit/python/spinor_submit/__main__.py)
  - argparse front (`submit`, `targets`, `providers`, `version`).
  Honours `--api-key-file` and `--api-key-stdin`; populates the
  canonical `IBM_QUANTUM_TOKEN` / `AWS_*` / `AZURE_*` env vars from
  the resolved secret. 8 new pytest tests, all green.
- **Release CI** at [`.github/workflows/release-cli.yml`](.github/workflows/release-cli.yml)
  - matrix build for `linux-x86_64`, `linux-aarch64`,
  `windows-x86_64`. Each artifact signed via
  `actions/attest-build-provenance` (Sigstore-backed). Tarballs
  publish as GitHub Release assets and mirror to
  `docs/site/content/downloads/`.
- **One-line install** scripts:
  - [`scripts/install.sh`](scripts/install.sh) - POSIX shell, detects
    `uname -sm`, downloads the right tarball, verifies provenance via
    `gh attestation verify` (with a SHA256 fallback), installs into
    `~/.local/heisenberg/`, symlinks `~/.local/bin/{spinorc,photonc,
    phononc,spinor-submit}`.
  - [`scripts/install.ps1`](scripts/install.ps1) - PowerShell sibling
    for Windows.
- **Docs site page** at
  [`docs/site/content/install/cli.md`](docs/site/content/install/cli.md)
  with install instructions, provenance verification, troubleshooting,
  and a Bell smoke test.
- **Demo simplification**: `~/heisenberg-demo/run_bell.sh` and
  `run_grover.sh` collapse to one-line `photonc run` invocations.
  New `run_artifact.sh` demonstrates the compile-here, submit-
  elsewhere flow via the QASM3 + manifest sidecar.

End-to-end smoke (cassette mode, no IBM token):

```
photonc run bell.pho --target ibm_heron_r2 --mode cassette
shots=1000 total counts=1000
histogram:
  |00>:   498  ###################
  |11>:   502  ####################
```

Decisions taken:

- **Subprocess to Python for the network hop**, not in-process
  libcurl. Vendor SDKs handle non-trivial auth (IBM Cloud IAM, AWS
  SigV4, Azure DefaultCredential); re-implementing in C++ is
  weeks per provider plus a permanent maintenance tail. The ~100 ms
  subprocess hop is invisible against any cloud round-trip.
- **No secrets in flags**. `--api-key-file` / `--api-key-stdin` /
  env only. `--help` documents this; the parser explicitly rejects
  `--api-key=<literal>` with a precise diagnostic.
- **`gh attestation verify`** for provenance instead of `cosign`.
  Smaller dependency footprint (already installed on most dev
  machines), and the attestation chain ends at GitHub's OIDC issuer
  with no human key material.
- **Auto-derived provider** from the chip YAML (`--target
  ibm_heron_r2` implies `--provider ibm`); explicit `--provider`
  overrides.

---

## 2026-06-17 — Steps 1+2: chip-coverage roadmap + FUTUREPLAN

What landed:

- **Step 1 (Bucket A — YAML only).** 18 new chip YAMLs under
  `spinor/registry/chips/` plus 9 new procedural topologies under
  `spinor/registry/topologies/`. Total chips in registry: 27 (up
  from 4). Coverage: IBM Heron r2/r3, Eagle (Brisbane, Sherbrooke),
  Osprey, Nighthawk r1, Torrino; Quantinuum H2-1, H1-1, Helios;
  IonQ Tempo, Forte, Forte Enterprise, Aria-1, Harmony, Aria-proto;
  Rigetti Ankaa-2, Ankaa-3, Ankaa-9Q-3; IQM Garnet, Emerald; OQC
  Toshiko; AQT Pine.
- **Step 2 (Bucket B — adapters).** Four new submission adapters in
  `spinor/submit/python/spinor_submit/__init__.py` (`_live_qci`,
  `_live_anyon`, `_live_tii`, `_live_alicebob`); 4 cassettes; 8 new
  unit tests; 4 new sections in the credentials guide. Live mode
  raises `RuntimeError` pointing at `chips_unsupported.md` because
  vendor production endpoints are not yet public.
- **Verification.** New `scripts/verify_chip_yamls.py` cross-checks
  every chip YAML against the C++ Lexer's gate set (read directly
  from `Lexer.cpp`), the topology directory, and runs a per-chip
  smoke compile against `spinorc compile`. Result: 27/27 pass
  schema + topology; 12 chips report a `[known-gap]` because the
  C++ KAK decomposer does not yet ship CZ/CX recipes (one PR will
  unblock all twelve). Also new
  `scripts/generate_topology_yamls.py` and
  `scripts/generate_chip_yamls.py` so the registry stays
  reproducible.
- **FUTUREPLAN.md** at the repo root (552 lines, 13 sections,
  clickable ToC). Mirrored on the docs site at
  `docs/site/content/futureplan.md` via a one-line snippet include.
  Sections: whole landscape, why we ship 26 today, competitor table,
  five-bucket support taxonomy, deep-dives for analog Rydberg /
  photonic / annealing DSLs (grammar, lowering, type checker,
  effort estimates), Phase E notes, why no new family fits Photon /
  Phonon / Spinor (concrete grammar / type / lowering arguments),
  decision matrix, glossary, references with verified-upstream
  dates.
- **Unsupported chips ledger** at
  `docs/site/content/chips_unsupported.md` enumerating every chip
  we do **not** ship and the *exact* piece of data we are missing
  (compiler-recipe gap, vendor blocker, decommission, or
  not-yet-written YAML), so a future contributor can fix one row in
  one PR.
- **`spinor_submit` test suite:** 14 passed, 1 skipped (live IBM
  needs a token).
- **`mkdocs build` (non-strict):** green; both new pages render.

Decisions taken:

- For chips whose data could not be fully verified upstream, the
  rule is: do NOT invent values; either ship a YAML with the
  `notes:` block carrying source URL + `verified-upstream` date, or
  put the chip on `chips_unsupported.md` with the missing-data
  column populated. No silent partials.
- The `verify_chip_yamls.py` script treats the C++ decomposer's
  missing CZ/CX recipe as a `[known-gap]` rather than a hard
  failure - it's a Phase A code change, not a YAML problem.

Open follow-ups (tracked in `chips_unsupported.md`):

- Add KAK-CZ / KAK-CX recipes to
  `spinor/passes/Decomposition.cpp` (~1 day) - unblocks twelve
  chips at once.
- Replace the procedural topology approximations with vendor-supplied
  edge lists when calibration ingestion lands.
- Add live-mode bodies to the 4 Step-2 adapters once each vendor
  publishes their production REST URL.

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

## 2026-06-16 — Docs site live on GitHub Pages

What landed:

- Repo pushed to <https://github.com/nimesh08/quantum-stack> (public,
  D8). Single Phase A-D commit + the docs additions on top.
- `docs/site/` — production reference site:
  - **MkDocs Material 9.7.6** with dark/light toggle, full-text
    search, mermaid via `pymdownx.superfences`.
  - **mkdocstrings 1.0.4 + mkdocstrings-python 2.0.4** auto-renders
    every public Python symbol in `jobsvc` (12 modules) and
    `calibration` (4 modules). Google-style Args / Returns / Raises
    / Examples on every public class and function.
  - **Redocly CLI 2.32.2** generates the REST reference page from
    the FastAPI OpenAPI snapshot.
  - **typedoc-plugin-markdown 4.12.0** generates the TypeScript
    reference from the playground's public surface (api client,
    hooks, components).
  - Landing page, 5-min quickstart, 4 tutorials (Bell, GHZ,
    add-a-chip, add-a-provider), prose guide (mirrors
    `phaseD_platform_guide.md`), operations runbook, decisions log,
    glossary, changelog.
- `.github/workflows/docs.yml` — builds the site on every push to
  `main`, deploys via `actions/deploy-pages@v4`.
- D7 (mkdocs-material maintenance mode) and D8 (public repo for
  free GitHub Pages) recorded.
- Top-level README rewritten with docs badge and a four-layer
  summary; `platform/README.md` points at the live URL.

Live: <https://nimesh08.github.io/quantum-stack/> — 200 OK.

Tests: **107/107** unchanged (80 jobsvc + 19 calibration + 8 playground).

Phase D complete; reference docs live; nothing more on the plan.

---

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

