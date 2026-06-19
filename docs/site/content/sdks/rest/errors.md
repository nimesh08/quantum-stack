# REST — Errors

Heisenberg returns errors as a small JSON envelope. Same shape on
every endpoint.

## Envelope

```json
{
  "detail": "human-readable message",
  "request_id": "9c1f...",
  "code": "budget_exceeded"
}
```

| Field | Always present | Meaning |
|-------|---------------|---------|
| `detail` | yes | One-line description suitable for showing in a UI. |
| `request_id` | yes | Echoed in the `x-request-id` response header; also in jobsvc's structured logs. |
| `code` | optional | Machine-readable error class, drawn from a closed enum. |

## HTTP codes by class

| HTTP | Class | Codes |
|------|-------|-------|
| 400 | `bad_request` | invalid JSON, malformed UUID. |
| 401 | `unauthenticated` | missing / invalid `X-API-Key` or JWT. |
| 402 | `budget_exceeded` | the cost gate rejected the job. |
| 403 | `forbidden` | endpoint requires admin; user is not. |
| 404 | `not_found` | unknown job, target, user. |
| 409 | `illegal_transition` | tried to cancel a Completed job. |
| 422 | `validation_error` | pydantic body validation failed. |
| 429 | `rate_limited` | per-user rate limiter triggered; check `Retry-After`. |
| 500 | `internal` | unexpected server error; safe to retry. |
| 502 | `provider_unreachable` | downstream provider returned a non-2xx. |
| 504 | `provider_timeout` | provider call timed out. |

## Retry policy

| Class | Retry | Notes |
|-------|-------|-------|
| 401 | no | re-authenticate or refresh first. |
| 402 | no | adjust the budget or shots; do not retry as-is. |
| 422 | no | fix the body. |
| 429 | yes, after `Retry-After` seconds | exponential back-off recommended. |
| 5xx | yes | exponential back-off, max 3 attempts; idempotency keys in v0.6. |

## Validation errors

A `422` carries `errors` with a per-field detail:

```json
{
  "detail": "validation error",
  "request_id": "9c1f...",
  "code": "validation_error",
  "errors": [
    { "loc": ["body", "shots"], "msg": "ensure this value is greater than 0",
      "type": "greater_than" }
  ]
}
```

The shape matches FastAPI / pydantic v2's standard error format.

## Cost-control rejection

A `402` is the cost gate. The body explains why:

```json
{
  "detail": "shot count would exceed your daily budget ($0.50 used of $1.00 cap)",
  "request_id": "9c1f...",
  "code": "budget_exceeded",
  "estimate": {
    "total_gates": 4,
    "two_qubit_gates": 1,
    "depth": 4,
    "shot_cost_usd": "0.5500"
  }
}
```

The `estimate` block is the same `ResourceEstimate` you would get
from `POST /api/v1/jobs/estimate`.

---

Heisenberg's REST API was designed and implemented by **Nimesh Cheedella**.
