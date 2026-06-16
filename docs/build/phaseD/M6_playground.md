# M6 — Playground (React 19 + Monaco)

End: a SPA where a developer logs in, writes a Bell program, picks
`generic` (or any chip), clicks Run, and watches the histogram come
back from the worker.

## Stack

- React **19.2.7** + react-dom 19.2.7
- @monaco-editor/react **^4.7.0** + monaco-editor 0.52
- Vite 6 + TypeScript 5.6
- TanStack Query 5 (server state)
- Zustand 5 (auth state)
- Recharts 2 (histogram)
- React Router 7 (`/`, `/jobs`, `/settings`, `/login`)

## Pages

- **`/login`** — email + password → JWT pair stored in localStorage.
- **`/`** — split-pane Playground: Monaco editor (60% width) +
  result panel with estimate, dollar cost, and histogram.
- **`/jobs`** — recent jobs (auto-refresh every 5s).
- **`/settings`** — budget editor.

## Monaco language definitions

Three Monarch grammars (`spinor.ts`, `phonon.ts`, `photon.ts`) —
the keywords come straight out of the Phase A/B/C lexers.

## API client

`src/api/jobs.ts` — typed `fetch` wrapper. Attaches the bearer
token; clears on 401 and propagates 402 detail (over budget) so the
UI can surface the structured rejection.

## Tests

- `tests/unit/Histogram.test.tsx` — chart container renders, empty
  histogram doesn't crash.
- `tests/unit/languages.test.ts` — spinor/phonon/photon grammars
  declare the expected keywords/gates.
- `tests/unit/api.test.ts` — bearer attached, 401 clears, 402
  detail surfaced.
- `tests/e2e/bell.spec.ts` — Playwright: login → write Bell →
  Run → histogram visible. Requires the stack up via compose.

8/8 vitest green; production `vite build` succeeds; `tsc -b`
clean. Playwright suite ships, gated to compose-mode.
