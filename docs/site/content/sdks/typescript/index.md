# TypeScript SDK

`@heisenberg/sdk` is a typed REST client plus a set of React Query
hooks. It is what the bundled playground itself uses, packaged
separately so you can build your own front-end on top of `jobsvc`.

```bash
npm install @heisenberg/sdk
```

| Export | What it gives you |
|--------|-------------------|
| `createClient({ baseUrl, apiKey })` | Low-level HTTP client. Returns a typed object with one method per endpoint, generated from the OpenAPI spec. |
| `useJob(id)` / `useJobs()` | React Query hooks for jobs. Cancellation, polling, optimistic updates. |
| `useTargets()` | List the chips known to this jobsvc instance. |
| `useEstimate(...)` | Resource estimate before submission (cost gate). |
| `useAuth()` | Login, refresh, logout, current user. |
| `histogramFromResult(result)` | Helper — turns a `Result` into a normalised `{ label, count }[]` for charting. |

## What is in this section

- [Install](install.md) — `npm install`, with React 19+ peer
  requirement.
- [Quickstart](quickstart.md) — five lines from `npm install` to a
  histogram on screen.
- [Tutorial](tutorial.md) — build a minimal playground from
  scratch.
- [Cookbook](cookbook.md) — auth, paging, file uploads,
  cancellation.
- [Reference](../../reference/typescript/index.md) — TypeDoc-generated
  API.

## A complete example

```tsx
import { useState } from "react";
import { createClient, useJobs } from "@heisenberg/sdk";

const client = createClient({
  baseUrl: "http://127.0.0.1:8080",
  apiKey: import.meta.env.VITE_HEISENBERG_API_KEY,
});

export function Jobs() {
  const { data, isLoading } = useJobs(client);
  if (isLoading) return <p>loading…</p>;
  return (
    <ul>
      {data!.items.map((j) => (
        <li key={j.id}>
          {j.target} — {j.shots} shots — {j.state}
        </li>
      ))}
    </ul>
  );
}
```

## How it relates to the playground

The bundled playground (`platform/playground/`) imports
`@heisenberg/sdk` instead of having its own private API client.
This is intentional — it proves the SDK works at non-trivial scale
and gives external consumers a reference UI.

You can:

- Use the SDK and build your own UI from scratch.
- Fork the playground and tweak.
- Embed individual SDK hooks inside an existing React app.

## Compile-time checks

Every endpoint, every request body, every response shape is typed
from `jobsvc`'s OpenAPI export. If you call
`client.jobs.create({ shots: "1000" })` (string instead of number)
you get a TypeScript error at the call site — not a 400 at runtime.

The types regenerate automatically on every `jobsvc` release.

---

Heisenberg's TypeScript SDK was designed and implemented by **Nimesh Cheedella**.
