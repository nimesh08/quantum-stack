# SDKs

Three SDKs and one REST API, all backed by the same compiler + jobsvc:

- **[Python](python/index.md)** — `heisenberg`, `photon`,
  `spinor-submit`, `jobsvc` packages on PyPI. The native habitat for
  application code that contains a quantum kernel.
- **[C++](cpp/index.md)** — `libspinor`, `libphonon`, `libphoton`.
  Used when you embed Heisenberg inside a high-performance app or
  build a custom front-end on top of the compiler.
- **[TypeScript](typescript/index.md)** — `@heisenberg/sdk` on npm.
  Typed REST client + React Query hooks; what the playground itself
  is built on.
- [REST](rest/index.html) — the `jobsvc` HTTP API. Anything that
  speaks HTTP can drive Heisenberg.

## Three ways to do the same thing

The same task — submit a Bell pair to `ibm_heron_r2`, 1000 shots,
get the histogram back — written in each SDK.

| Task | Python | C++ | TypeScript |
|------|--------|-----|------------|
| Compile + submit | `photon.run(bell, target="ibm_heron_r2", shots=1000)` | `spinorc submit -t ibm_heron_r2 --shots 1000 bell.qasm3` | `await api.jobs.create({source, source_kind, target, shots})` |
| Cost estimate | `photon.estimate(bell, target="ibm_heron_r2")` | `spinorc check -t ibm_heron_r2 bell.spn` | `useEstimate(jobId)` |
| Watch progress | `client.poll(job_id)` | `spinorc submit ... --wait` | `useJob(jobId)` (streaming) |
| Download histogram | `client.results(job_id)` | `cat job-id.json` | `useResult(jobId)` |

## How the SDKs map to the engine

```mermaid
flowchart LR
  py[Python\nphoton + spinor-submit + jobsvc] --> nano[nanobind\nphoton._engine]
  cpp[C++\nlibspinor + libphonon + libphoton] --> engine[(C++ engine)]
  ts[TypeScript\n@heisenberg/sdk] -->|HTTP| jobsvc
  jobsvc -->|nanobind| engine
  nano --> engine
  engine -->|chip-locked artefact| provider[Provider layer]
```

The three SDKs are doors to the same compiler. RULE 3 (one C++
engine, one source of truth) is what makes that possible: every
language layer calls into the same engine instead of reimplementing
compilation.

## Picking an SDK

| Pick ... | When ... |
|----------|----------|
| **Python** | You write data-science / ML code; you want the OO Photon decorator; you want the fastest path from idea to histogram. |
| **C++** | You embed Heisenberg in a high-performance app; you ship a custom front-end; you contribute to the engine. |
| **TypeScript** | You build a UI on top of jobsvc; you want compile-time-checked REST calls; you reuse `@heisenberg/sdk`'s React Query hooks. |
| **REST** | You drive Heisenberg from a language not on this list; you write a CI integration; you wire it into a workflow engine. |

## Conventions every SDK follows

- **Cassette mode by default.** Tests and CI runs replay recorded
  provider responses so no cloud account is needed. Set
  `SPINOR_SUBMIT_MODE=live` to flip every adapter to real-cloud
  submission.
- **Verbatim submission only.** No SDK silently re-transpiles your
  program. The provider runs what the compiler produced.
- **Server returns ownership.** Histograms come back as
  `{ "00": 503, "11": 497 }` — no rescaling, no smoothing. Whatever
  the chip produced.

---

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
