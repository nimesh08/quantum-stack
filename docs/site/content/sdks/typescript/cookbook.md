# TypeScript SDK — cookbook

Recipes for the typed REST client and the hooks.

## Login flow with refresh token

```ts
import { useAuth } from "@heisenberg/sdk";
import { client } from "./client";

function LoginButton() {
  const { mutate: login, isPending } = useAuth.login(client);
  return (
    <button
      disabled={isPending}
      onClick={() =>
        login({ email: "admin@local", password: "admin-password" })
      }
    >
      Sign in
    </button>
  );
}
```

The hook stores the JWT in `localStorage` (configurable) and adds an
interceptor that attaches it to every subsequent request. Refreshes
happen automatically when the access token expires.

## Paginate the jobs list

```ts
import { useJobs } from "@heisenberg/sdk";
import { client } from "./client";

function JobsList() {
  const {
    data,
    fetchNextPage,
    hasNextPage,
  } = useJobs.infinite(client, { pageSize: 50 });

  return (
    <>
      {data?.pages.flatMap((p) => p.items).map((j) => (
        <div key={j.id}>{j.target}</div>
      ))}
      {hasNextPage && (
        <button onClick={() => fetchNextPage()}>load more</button>
      )}
    </>
  );
}
```

The cursor in the response is opaque; the hook handles it for you.

## Cancel an in-flight job

```ts
const { mutate: cancel } = client.jobs.cancel(jobId);
cancel(); // returns a promise; rejects if the job was already terminal
```

## Estimate the cost before submitting

```tsx
import { useEstimate } from "@heisenberg/sdk";
import { client } from "./client";

function CostPreview({ source, target, shots }: Props) {
  const { data: est } = useEstimate(client, {
    source,
    source_kind: "photon",
    target,
    shots,
  });
  if (!est) return null;
  return (
    <div>
      cost: ${est.shot_cost_usd.toFixed(4)} for {est.depth}-deep,{" "}
      {est.two_qubit_gates} two-qubit gates
    </div>
  );
}
```

## Mint a new API key from the UI

```ts
const { plaintext } = await client.users.createApiKey({ label: "ci-runner" });
console.log("save this:", plaintext);
```

The plaintext key is returned exactly once; show it in a modal with
a copy button.

## Use the SDK without React

Server-side or in a CLI:

```ts
import { createClient } from "@heisenberg/sdk";

const client = createClient({
  baseUrl: process.env.HEISENBERG_URL!,
  apiKey:  process.env.HEISENBERG_API_KEY!,
});

const job = await client.jobs.create({
  source: BELL_SOURCE,
  source_kind: "spinor",
  target: "ibm_heron_r2",
  shots: 1000,
});

while (true) {
  const j = await client.jobs.get(job.id);
  if (j.state === "Completed") {
    console.log(j.result!.counts);
    break;
  }
  await new Promise((r) => setTimeout(r, 500));
}
```

No React, no peer dependencies; just `fetch` under the hood.

---

Heisenberg's TypeScript SDK was designed and implemented by **Nimesh Cheedella**.
