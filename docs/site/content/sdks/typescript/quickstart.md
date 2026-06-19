# TypeScript SDK — quickstart

Five lines from `npm install` to a histogram in your browser.

## 1. Install

```bash
npm install @heisenberg/sdk @tanstack/react-query react react-dom
```

## 2. Wire React Query

`src/main.tsx`:

```tsx
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { createRoot } from "react-dom/client";
import { App } from "./App";

const qc = new QueryClient();

createRoot(document.getElementById("root")!).render(
  <QueryClientProvider client={qc}>
    <App />
  </QueryClientProvider>,
);
```

## 3. Submit a Bell job

`src/App.tsx`:

```tsx
import { useState } from "react";
import { createClient, useJob } from "@heisenberg/sdk";

const client = createClient({
  baseUrl: "http://127.0.0.1:8080",
  apiKey:  import.meta.env.VITE_HEISENBERG_API_KEY!,
});

const BELL_SOURCE = `
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
`;

export function App() {
  const [jobId, setJobId] = useState<string | null>(null);
  const { data: job } = useJob(client, jobId);

  async function submit() {
    const j = await client.jobs.create({
      source:      BELL_SOURCE,
      source_kind: "spinor",
      target:      "ibm_heron_r2",
      shots:       1000,
    });
    setJobId(j.id);
  }

  return (
    <div>
      <button onClick={submit}>Run Bell</button>
      {job?.state === "Completed" && (
        <pre>{JSON.stringify(job.result?.counts, null, 2)}</pre>
      )}
    </div>
  );
}
```

## 4. Run

```bash
npm run dev
```

Open `http://localhost:5173/`, click **Run Bell**, see
`{"00": 503, "11": 497}`.

## What `useJob` does for you

- Polls `GET /api/v1/jobs/{id}` until the state is terminal.
- Caches the response so multiple components reading the same job
  share a single network call.
- Cancels in-flight requests on unmount.
- Re-runs after a successful submit (mutation invalidates the
  cache).

## Where to next

- [Tutorial](tutorial.md) — build a minimal playground from
  scratch.
- [Cookbook](cookbook.md) — auth, paging, file uploads.
- [Reference](../../reference/typescript/index.md) — every typed
  endpoint and hook.

---

Heisenberg's TypeScript SDK was designed and implemented by **Nimesh Cheedella**.
