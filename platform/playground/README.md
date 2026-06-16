# Playground (Phase D)

React 19.2 + Monaco SPA. The browser front door for the Heisenberg
quantum-stack.

## Quickstart

```bash
cd platform/playground
npm install
npm run dev          # http://localhost:5173, proxies /api -> :8000
```

Make sure `jobsvc` is running on `:8000` and at least one
`jobsvc-worker` is processing the queue.

## Tests

```bash
npm test              # Vitest unit (fast)
npm run typecheck     # tsc -b
npm run build         # production build (vite)
npm run test:e2e      # Playwright (needs the full stack up)
```

## Pinned versions

See `package.json` and `phaseD_platform_guide.md`.
