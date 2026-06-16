# Cookbook — `jobsvc` (curl + Python recipes)

## 1. Login

```bash
TOKEN=$(curl -s -X POST http://localhost:8000/api/v1/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"admin@local","password":"admin-password"}' | jq -r .access_token)
```

## 2. Submit a Bell program

```bash
curl -s -X POST http://localhost:8000/api/v1/jobs \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
        "source": "target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
        "source_kind": "spinor",
        "target": "generic",
        "shots": 1000
      }' | jq
```

## 3. Poll for completion

```bash
JID=...
while true; do
    s=$(curl -s -H "Authorization: Bearer $TOKEN" \
        http://localhost:8000/api/v1/jobs/$JID | jq -r .state)
    echo $s
    [[ "$s" =~ ^(Completed|Rejected|Failed)$ ]] && break
    sleep 1
done
```

## 4. Get the histogram

```bash
curl -s -H "Authorization: Bearer $TOKEN" \
    http://localhost:8000/api/v1/jobs/$JID | jq .result.counts
```

## 5. Tighten the budget

```bash
curl -s -X PATCH -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    -d '{"daily_usd":"0.10"}' \
    http://localhost:8000/api/v1/me/budget
```

## 6. Make an API key for scripts

```bash
curl -s -X POST -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    -d '{"label":"my-script"}' \
    http://localhost:8000/api/v1/me/api-keys | jq
# {"plaintext":"Q4r2p8aA.GvXc...","prefix":"Q4r2p8aA",...}
```

Then use it with the `X-API-Key: <plaintext>` header instead of
`Authorization: Bearer …`.

## 7. List your jobs

```bash
curl -s -H "Authorization: Bearer $TOKEN" \
    'http://localhost:8000/api/v1/jobs?limit=20' | jq .items
```

## 8. Cancel a queued job

```bash
curl -s -X DELETE -H "Authorization: Bearer $TOKEN" \
    http://localhost:8000/api/v1/jobs/$JID
```

## See also

[REST reference](../../api/rest/index.html), [SDK examples](sdk_examples.md)
