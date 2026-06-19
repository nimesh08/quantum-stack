# Reference — TypeScript

TypeDoc-generated reference for `@heisenberg/sdk` and the bundled
playground's internal modules.

The auto-generated pages live alongside this index; if you are
reading this in the source tree before the docs CI has run, see
[`platform/playground/typedoc.json`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/typedoc.json)
for the generator config.

## Top-level modules

| Module | What it gives you |
|--------|-------------------|
| `@heisenberg/sdk` | `createClient`, `useJob`, `useJobs`, `useTargets`, `useEstimate`, `useAuth`. |
| `playground/src/api/jobs.ts` | Internal — the playground's typed client; mirrors `@heisenberg/sdk`. |
| `playground/src/components/Editor.tsx` | Monaco wrapper + Spinor / Phonon / Photon language registrations. |
| `playground/src/components/Histogram.tsx` | Recharts bar-chart for `Result.counts`. |
| `playground/src/components/TargetPicker.tsx` | `<select>` over `useTargets()`. |
| `playground/src/languages/{spinor,phonon,photon}.ts` | Monaco language definitions. |

The narrative SDK pages
([SDKs / TypeScript](../../sdks/typescript/index.md)) are the right
place to learn what each export does; this section is the
dictionary.

---

Heisenberg's TypeScript API was authored by **Nimesh Cheedella**.
