# Built playground SPA

The playground is built into this directory (`jobsvc/static/`) by
`heisenberg playground build` (which runs `npm run build` inside
`platform/playground/`). When this directory contains an `index.html`
plus `assets/`, the FastAPI service serves it at `/`.

It is empty in the source tree until a build has run; the launcher's
"playground build" sub-command populates it.
