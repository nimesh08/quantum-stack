# Install — `playground`

The Phase D React 19.2 + Monaco SPA.

## Quickstart (developer)

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack/platform/playground
npm install
npm run dev                  # http://localhost:5173
```

Vite proxies `/api`, `/healthz`, `/readyz` to `http://localhost:8000`,
so [`jobsvc`](../jobsvc/install.md) needs to be running there too.

## Production build

```bash
npm run build                # emits dist/
```

`dist/` is what nginx serves at `/` per
[Server runbook §4c](../../install/server_systemd.md).

## See also

- [Dev workflow](dev_workflow.md)
- [Customising Monaco](customising.md)
- [TypeScript autodoc](../../api/typescript/index.md)
