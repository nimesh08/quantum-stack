# Phase D — decisions (deviations from the Platform Services Deep-Dive)

For the implementation journal see
[`phaseD_progress.md`](phaseD_progress.md). For the end-of-phase
guide see [`phaseD_platform_guide.md`](phaseD_platform_guide.md).

---

## D1 — Postgres-as-queue (no Redis, no Celery)

The deep-dive says "background worker(s) + queue" without specifying a
substrate. We chose Postgres-as-queue:
`SELECT … FOR UPDATE SKIP LOCKED` with `LISTEN/NOTIFY` for wake-up.

Rationale:

- Zero new infrastructure (we already require Postgres for jobs/results).
- Transactional with the job state machine — claim and `state=Running`
  in one row update; no two-phase commit risk between MQ and DB.
- FastAPI-sized service; queue depths in the hundreds, not millions.
- Kept behind `jobsvc.queue` interface so we can swap to Redis+RQ
  later if scale demands without touching call-sites.

Alternatives considered: Redis+RQ (extra container), Celery (heavy),
NATS JetStream (overkill). Reject reason: each adds a moving part for
no measurable benefit at v1 traffic.

## D2 — Auth: local JWT + API keys (no external IdP)

The deep-dive says "standard web-app auth, per-user budgets". We ship
both at v1:

- **Username + password → JWT (HS256, 60-min access, 14-day refresh)** —
  used by the Playground.
- **API keys (`X-API-Key`)** — used for programmatic submission.

Roles: `user`, `admin`. No external IdP; the deploy operator can layer
OIDC (Authlib) on top later if needed. Password reset and email
verification are out-of-scope at v1; admin seeds users via a CLI.

## D3 — One nanobind addition (`emit_qasm3_verbatim`)

The Phase C binding exposed `dump_phonon`, `dump_spinor`, `estimate`
but not the OpenQASM 3 emitter the workers need to send to providers.
We added one C++→Python function:

```cpp
m.def("emit_qasm3_verbatim",
      [](const CompiledProgram& cp) -> std::string { … });
```

This is a **pure surface-area addition** — it calls the existing Phase A
QASM 3 emitter with `verbatim=true`. No compiler-logic change. Justified
as the smallest possible touch to Phase C; documented at
[`photon/bindings/python/Module.cpp`](../../photon/bindings/python/Module.cpp).

## D4 — APScheduler with Postgres job-store

The calibration scheduler uses APScheduler 3.x with
`SQLAlchemyJobStore` so triggers survive restart. Single-replica by
design — calibration is idempotent so no leader election is needed at
v1.

## D5 — AWS/Azure calibration providers as stubs at v1

IBM calibration is fetched live via `qiskit-ibm-runtime`'s
`backend.properties()`. AWS and Azure providers ship as stubs that
log "not yet implemented" and keep the previous calibration file
intact. Rationale: each provider's calibration API surface differs
substantially and live verification against AWS/Azure is gated by
account access not present in CI. Documented as future work; the
fixture provider covers all three for offline tests.

## D6 — `source_kind` accepts photon/phonon/spinor

`POST /jobs` accepts all three source forms. Photon is lowered via the
existing Phase C entry points; Phonon hits the binding directly;
Spinor (.spn) is wrapped as a trivial Phonon program. This realises
the deep-dive's promise that the API takes "Photon / Phonon / Spinor
source, or an uploaded IR" without forcing a v1.5 follow-up.
