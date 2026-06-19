# REST — Pagination

Heisenberg uses **opaque cursor pagination**. No page numbers, no
`offset`. Cursors are opaque strings; never parse them client-side
— they encode `(created_at, id)` and the encoding may change.

## Shape

`GET /api/v1/jobs?limit=50` returns:

```json
{
  "items": [ /* ... up to 50 jobs ... */ ],
  "next_cursor": "v1.eyJjcmVhdGVkX2F0Ijoi..."
}
```

When `next_cursor` is `null`, you have reached the end.

To fetch the next page, pass the cursor back:

```bash
curl -fsS "http://127.0.0.1:8080/api/v1/jobs?limit=50&cursor=$CURSOR" \
  -H "X-API-Key: $KEY"
```

## Limits

| Parameter | Default | Max |
|-----------|---------|-----|
| `limit` | 50 | 100 |

Asking for more than 100 returns a 422.

## Stability

Pages are stable across pagination. Inserting a new row at the head
of the table does not duplicate or skip rows in subsequent
requests, because the cursor encodes the timestamp of the last seen
row.

Deleting a row mid-pagination is fine — that row simply no longer
appears.

## Why opaque cursors

| Problem | Page numbers / offset | Cursors |
|---------|----------------------|---------|
| Insertions during pagination | rows can shift; client sees duplicates | stable |
| Deep paging cost | `OFFSET 100000` is O(N) on Postgres | O(1) on the index |
| Schema evolution | offset semantics are baked in | cursor encoding can change without breaking clients |

If you need a different style for a specific endpoint, file an issue
— we will discuss adding a deterministic cursor scheme rather than
reverting to offsets.

## TypeScript helper

```ts
import { useJobs } from "@heisenberg/sdk";
import { client } from "./client";

const {
  data,
  fetchNextPage,
  hasNextPage,
} = useJobs.infinite(client, { pageSize: 50 });

// `data.pages` is an array of pages; flatten as needed:
const allJobs = data?.pages.flatMap((p) => p.items) ?? [];
```

The hook handles the cursor for you.

---

Heisenberg's REST API was designed and implemented by **Nimesh Cheedella**.
