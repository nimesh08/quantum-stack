# REST — Authentication

Two parallel auth schemes:

| Scheme | Header | When to use |
|--------|--------|-------------|
| API key | `X-API-Key: <prefix>.<body>` | Headless integrations, CI, scripts. Long-lived; rotate explicitly. |
| JWT (HS256) | `Authorization: Bearer <token>` | Browsers, the playground. Short-lived (60 min); refresh token granted by `/login`. |

Both authenticate to the same `User` row. API keys carry an explicit
`label` so you can revoke one without affecting the others.

## API keys

### Mint a new key

`heisenberg seed` creates one at install time. To mint another:

```bash
curl -fsS http://127.0.0.1:8080/api/v1/users/me/api-keys \
  -H "X-API-Key: $EXISTING_KEY" \
  -H "Content-Type: application/json" \
  -d '{"label":"ci-runner"}'
```

Response (the plaintext is shown **once**):

```json
{
  "id": "f2b1a4...",
  "label": "ci-runner",
  "prefix": "Q4r2p8aA",
  "plaintext": "Q4r2p8aA.<32-char-body>",
  "created_at": "2026-06-19T11:36:00Z"
}
```

### Use a key

Every authenticated endpoint accepts the `X-API-Key` header:

```bash
curl -fsS http://127.0.0.1:8080/api/v1/users/me \
  -H "X-API-Key: Q4r2p8aA.<32-char-body>"
```

### Revoke a key

```bash
curl -fsS -X DELETE \
  http://127.0.0.1:8080/api/v1/users/me/api-keys/$KEY_ID \
  -H "X-API-Key: $YOUR_KEY"
```

The key's `revoked_at` timestamp is set; subsequent requests with
that key return 401.

### Storage

Server-side, only the 8-character prefix and the bcrypt hash of the
full plaintext are stored. The prefix lets us index lookups; the
hash gates verification. We never recover plaintext.

## JWT login

### Login

```bash
curl -fsS http://127.0.0.1:8080/api/v1/login \
  -H "Content-Type: application/json" \
  -d '{"email":"admin@local","password":"admin-password"}'
```

Response:

```json
{
  "access_token":  "eyJ...short-lived",
  "refresh_token": "eyJ...14-day",
  "token_type":    "Bearer",
  "expires_in":    3600
}
```

### Use the access token

```bash
curl -fsS http://127.0.0.1:8080/api/v1/users/me \
  -H "Authorization: Bearer $ACCESS_TOKEN"
```

### Refresh

```bash
curl -fsS http://127.0.0.1:8080/api/v1/refresh \
  -H "Content-Type: application/json" \
  -d '{"refresh_token":"<the-refresh-token>"}'
```

The TypeScript SDK's `useAuth` hook handles refresh automatically.

## Anonymous endpoints

| Path | Why |
|------|-----|
| `/healthz` | liveness probe |
| `/readyz` | readiness probe (DB connectivity) |
| `/metrics` | Prometheus exposition; protect via reverse proxy if needed |
| `/api/openapi.json` | spec download |
| `/api/docs` | interactive Swagger UI |

Everything under `/api/v1` requires authentication.

## Errors

| HTTP | Meaning |
|------|---------|
| 401 | Missing / invalid auth header. |
| 402 | Cost-control rejection (over budget). |
| 403 | Authenticated but not allowed (admin-only endpoint). |
| 404 | Object not found. |
| 422 | Body validation error. |
| 429 | Rate-limited. |
| 5xx | Server error; safe to retry with exponential back-off. |

See [REST / Errors](errors.md) for the full envelope shape.

---

Heisenberg's REST API was designed and implemented by **Nimesh Cheedella**.
