# Dev workflow — `playground`

## Hot reload

```bash
npm run dev
```

Vite watches `src/` and reloads on save. `tsc -b` runs in the
background; type errors appear in the terminal.

## Test

```bash
npm test                  # vitest run (unit; ~8 tests)
npm run test:e2e          # Playwright; needs the full stack
npm run typecheck         # tsc -b --noEmit
```

## Build for production

```bash
npm run build             # vite build → dist/
npm run preview           # serve dist/ locally for one-off checks
```

## Configure the API base

By default the SPA calls same-origin `/api`. To target a different
host (e.g. when serving the SPA on GitHub Pages while the API lives
on your server):

```bash
VITE_API_BASE=https://your.server.com npm run build
```

(Pattern B in the [server runbook](../../install/server_systemd.md#12-pattern-b-playground-on-github-pages-api-on-your-server).)

## See also

[Customising Monaco](customising.md), [Install](install.md)
