# REST API

The `jobsvc` HTTP API is the only thing every Heisenberg client
talks to. Anything that can speak HTTP — `curl`, your CI runner,
your favourite scripting language — drives Heisenberg.

## Base URL

| Environment | URL |
|-------------|-----|
| `heisenberg run` (laptop) | `http://127.0.0.1:8080` |
| systemd production | whatever the reverse proxy fronts it with |
| Inside `jobsvc` itself | mounted at `/api/v1` of the FastAPI app |

The OpenAPI spec is exposed at `GET /api/openapi.json`. The
interactive docs are at `GET /api/docs` (Swagger UI). The Redoc
embed lives in [Reference / REST](../../reference/rest/index.html).

## What is in this section

- [Authentication](authentication.md) — JWT, API keys, refresh.
- [Errors](errors.md) — error envelope, HTTP codes, retry policy.
- [Pagination](pagination.md) — opaque cursors, page sizes.
- [Reference](../../reference/rest/index.html) — the full Redoc
  embed of the OpenAPI spec; one page per endpoint.

## A complete example

Submit a Bell job and read the result:

```bash
URL=http://127.0.0.1:8080
KEY=Q4r2p8aA.<your-32-char-body>

# Submit.
JOB=$(curl -fsS "$URL/api/v1/jobs" \
  -H "X-API-Key: $KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "source":"target generic\nqubit q[2]\nbit c[2]\nh q[0]\ncx q[0],q[1]\nc=measure q\n",
    "source_kind":"spinor",
    "target":"ibm_heron_r2",
    "shots":1000
  }' | jq -r .id)
echo "job id: $JOB"

# Poll until terminal.
while :; do
  STATE=$(curl -fsS "$URL/api/v1/jobs/$JOB" \
                -H "X-API-Key: $KEY" | jq -r .state)
  case "$STATE" in
    Completed|Failed|Rejected) break;;
  esac
  sleep 0.5
done

# Histogram.
curl -fsS "$URL/api/v1/jobs/$JOB/result" -H "X-API-Key: $KEY" | jq .counts
```

You see `{"00": 503, "11": 497}`.

## Endpoint catalog

| Method | Path | What it does |
|--------|------|--------------|
| `POST` | `/api/v1/login` | Email + password → JWT pair. |
| `POST` | `/api/v1/refresh` | Refresh token → new access JWT. |
| `GET` | `/api/v1/users/me` | Current user. |
| `POST` | `/api/v1/users/me/api-keys` | Mint a new API key. |
| `PUT` | `/api/v1/users/me/budget` | Set daily / monthly / per-job limits. |
| `GET` | `/api/v1/targets` | List chips known to this jobsvc. |
| `POST` | `/api/v1/jobs` | Submit a new job. Returns 402 if it would blow your budget. |
| `GET` | `/api/v1/jobs` | Paged list of your jobs (cursor). |
| `GET` | `/api/v1/jobs/{id}` | One job. |
| `GET` | `/api/v1/jobs/{id}/result` | Histogram + raw provider payload. |
| `POST` | `/api/v1/jobs/{id}/cancel` | Cancel a non-terminal job. |
| `GET` | `/healthz` | Liveness. No auth. |
| `GET` | `/readyz` | Readiness (DB connectivity). No auth. |
| `GET` | `/metrics` | Prometheus exposition. No auth. |

Full schemas, request bodies, and response shapes:
[Reference / REST](../../reference/rest/index.html).

## Conventions

- All times are ISO 8601 UTC.
- Money is `Decimal` strings (`"0.0050"`), never floats.
- IDs are UUIDv4 strings.
- Cursors are opaque strings; never parse them client-side.

---

Heisenberg's REST API was designed and implemented by **Nimesh Cheedella**.
