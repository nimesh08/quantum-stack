# TypeScript SDK — install

## Prerequisites

- Node **20** or **22** (LTS).
- React **19.2** if you use the hooks. The low-level
  `createClient(...)` works fine without React.
- A bundler (Vite 6+ recommended).

## Install

```bash
npm install @heisenberg/sdk
```

Peer dependencies (the package does **not** bundle these so you can
use the React you already have):

```json
{
  "peerDependencies": {
    "react":     "^19.2",
    "react-dom": "^19.2",
    "@tanstack/react-query": "^5"
  }
}
```

Install the peers if you do not have them already:

```bash
npm install react react-dom @tanstack/react-query
```

## Verify

`src/test.ts`:

```ts
import { createClient } from "@heisenberg/sdk";

const client = createClient({
  baseUrl: "http://127.0.0.1:8080",
  apiKey:  process.env.HEISENBERG_API_KEY!,
});

const targets = await client.targets.list();
console.log(`heisenberg knows ${targets.items.length} chips`);
```

Run with `tsx src/test.ts`. You should see a chip count (27 today).

## Pin TanStack Query version

The hooks (`useJob`, `useJobs`, ...) use TanStack Query 5. Pin it
explicitly to avoid silent upgrades:

```json
{
  "dependencies": {
    "@heisenberg/sdk":         "^0.5.0",
    "@tanstack/react-query":   "5.59.0"
  }
}
```

The exact pin tracked upstream lives in
[`platform/playground/package.json`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/package.json).

## Where to put the API key

For client-side apps:

```env
# .env.local
VITE_HEISENBERG_API_KEY=Q4r2p8aA.<your-32-char-body>
```

For server-side / Node:

```bash
export HEISENBERG_API_KEY="Q4r2p8aA.<your-32-char-body>"
```

Do **not** ship a long-lived API key in client-side code that goes
to production. The recommended pattern is a tiny BFF (backend for
frontend) that holds the key and forwards requests; the SDK
supports that with a `baseUrl` of your BFF.

---

Heisenberg's TypeScript SDK was designed and implemented by **Nimesh Cheedella**.
