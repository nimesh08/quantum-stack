# `@heisenberg/sdk`

Typed REST client plus React Query hooks for the Heisenberg
Quantum Stack `jobsvc`. The bundled playground at
[`platform/playground/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/playground)
imports from this package, so it is the same SDK you build a
custom UI against.

```bash
npm install @heisenberg/sdk
```

## Two ways to use it

### Low-level — typed `fetch`

```ts
import { createClient } from "@heisenberg/sdk";

const client = createClient({
  baseUrl: "http://127.0.0.1:8080",
  apiKey:  process.env.HEISENBERG_API_KEY!,
});

const job = await client.jobs.create({
  source:      "target generic\nqubit q[2]\nbit c[2]\nh q[0]\ncx q[0],q[1]\nc=measure q\n",
  source_kind: "spinor",
  target:      "ibm_heron_r2",
  shots:       1000,
});
```

### High-level — React Query hooks

```tsx
import { createClient, useJob, useJobs, useTargets } from "@heisenberg/sdk";

const client = createClient({ baseUrl, apiKey });

function MyJobsList() {
  const { data } = useJobs(client);
  return <ul>{data?.items.map((j) => <li key={j.id}>{j.target}</li>)}</ul>;
}
```

The hooks require React 19.2+ and TanStack Query 5+ as peer
dependencies; if you only use `createClient`, those peers are
optional.

## Documentation

<https://nimesh08.github.io/quantum-stack/sdks/typescript/>.

Author: **Nimesh Cheedella**.
