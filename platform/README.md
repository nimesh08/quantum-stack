# Platform — Phase D

Platform services. **Not** built by the top-level CMake project: these are
different tech stacks (Python/FastAPI + React) and have their own tooling.

## Subfolders

| Folder         | Purpose                                                |
|----------------|--------------------------------------------------------|
| `jobsvc/`      | FastAPI service + workers + queue.                     |
| `calibration/` | Nightly refresh job.                                   |
| `playground/`  | React 19 + Monaco SPA.                                 |
| `deploy/`      | Dockerfiles, compose.                                  |

## The one allowed future split

`playground/` and `jobsvc/` MAY be extracted to their own repository later
if monorepo friction outweighs benefit. **Start them inside the monorepo.
Do not pre-split.**

## Critical rules that bind this phase

- **RULE 1** — Do not start Phase D until Phase C's tests pass.
- **RULE 5** — Job service submits to providers verbatim only.

## Status

Empty skeleton. Phase D starts after Phase C is done.
