# M2 — FastAPI surface + engine call

End: a working FastAPI app whose `POST /api/v1/jobs` calls the
compiler engine, returns a `ResourceEstimate`, persists a `Job` in
the right state.

## Endpoints (versioned `/api/v1`)

| Method | Path | Purpose |
|--------|------|---------|
| POST   | /login | Email + password → JWT pair |
| GET    | /me   | Current user |
| GET    | /me/budget | Read budget (auto-creates default) |
| PATCH  | /me/budget | Update daily/monthly/max_shots |
| POST   | /me/api-keys | Generate API key (plaintext returned once) |
| GET    | /me/api-keys | List my keys |
| DELETE | /me/api-keys/{id} | Revoke |
| GET    | /targets | Chips known to the registry |
| GET    | /targets/{id} | One chip |
| POST   | /jobs | Compile + cost-check + queue |
| GET    | /jobs | Paged list (cursor) |
| GET    | /jobs/{id} | Detail (incl. histogram if Completed) |
| DELETE | /jobs/{id} | Cancel (only Submitted/Queued) |
| POST   | /admin/users | Admin-only user create |
| GET    | /admin/users | Admin-only list |
| GET    | /healthz, /readyz, /metrics | Health + Prometheus |

## Engine seam

`jobsvc.engine.compile_program(source, source_kind, target)` wraps
`photon._engine.compile_phonon`. When the binding isn't present
on the host (CI without LLVM) a deterministic stub kicks in so the
platform can be developed end-to-end without the C++ engine.

## Tests

23 integration tests via `httpx.AsyncClient(ASGITransport)` plus
an in-memory SQLite + seeded users. Plus the unit tests from M1.

49/49 green at end of M2 (26 unit + 23 integration).
