# Playground

The Phase D React + Monaco SPA. Browser front door — write a program,
click Run, see a histogram.

## Install (developer mode)

### 1. Get Node 22

```bash
nvm install 22 && nvm use 22
node --version       # v22.x
```

### 2. Install deps

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/playground
npm install
```

### 3. Start the dev server

```bash
npm run dev
# Local:   http://localhost:5173
```

Vite proxies `/api`, `/healthz`, `/readyz` to `http://localhost:8000`,
so [`jobsvc`](jobsvc.md) needs to be running on `:8000` first.

### 4. Smoke test

Open <http://localhost:5173>, log in (`admin@local` / `admin-password`),
the Bell program is preloaded. Click **Run**.

## Other scripts

```bash
npm run typecheck     # tsc -b
npm run build         # vite build -> dist/
npm run preview       # serve dist/ locally
npm test              # vitest run (8 tests)
npm run test:e2e      # Playwright; needs the full stack
```

## Pinned versions

| Package | Pin | Why |
|---|---|---|
| react | 19.2.7 | latest 19.2.x security patch |
| react-dom | 19.2.7 | match |
| @monaco-editor/react | ^4.7.0 | first stable with React 19 peer |
| monaco-editor | ^0.52.2 | peer of monaco-react |
| vite | ^6 | TS 5.6 + Node 22 friendly |
| typescript | ^5.6 | strict mode on |
| @tanstack/react-query | ^5 | server state |
| zustand | ^5 | auth state |
| recharts | ^2 | histogram |
| @playwright/test | ^1.48 | e2e |
| vitest | ^2 | unit tests |

Authoritative: [`platform/playground/package.json`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/package.json).

## See also

- [TypeScript autodoc](../api/typescript/index.md)
- [Customising Monaco languages](../packages/playground/customising.md)
