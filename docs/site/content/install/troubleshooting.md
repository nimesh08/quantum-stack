# Troubleshooting

Common install errors and what to do about them.

## Build / engine

| Symptom | Likely cause | Fix |
|---|---|---|
| `error: Could NOT find Eigen3` at cmake configure | Eigen 5 not installed | `apt install libeigen3-dev` (Ubuntu 24.04+ ships ≥5.0). Otherwise build from `gitlab.com/libeigen/eigen` v5.0.1. |
| `nanobind not found` at cmake configure | nanobind not on PYTHONPATH | `pip install nanobind` then re-configure with `-Dnanobind_DIR=$(python3 -c 'import nanobind; print(nanobind.cmake_dir())')`. |
| `clang: error: invalid argument '-std=c++20'` | older compiler | Install GCC 13+ or Clang 17+; set `CMAKE_CXX_COMPILER`. |
| `LLVM_DIR-NOTFOUND` warnings | LLVM not present | Harmless — the in-tree path builds. To enable the MLIR-backed bridge install LLVM 22.1.8. |

## Python

| Symptom | Likely cause | Fix |
|---|---|---|
| `ImportError: photon._engine` | nanobind extension not built | Build engine from source, then add `build/photon/bindings/python` to `PYTHONPATH`. |
| `ValueError: password cannot be longer than 72 bytes` | bcrypt 4.x with passlib 1.7.4 | Pin `bcrypt>=3.2,<4.1` (already done in `platform/jobsvc/pyproject.toml`). |
| `psycopg.errors.UndefinedTable: jobs` | migrations not run | `alembic upgrade head` from `platform/jobsvc/`. |
| `JOBSVC_DATABASE_URL not set` (test skip) | env var missing | `export JOBSVC_DATABASE_URL=postgresql+asyncpg://jobsvc:jobsvc@localhost:5432/jobsvc`. |

## Node / Playground

| Symptom | Likely cause | Fix |
|---|---|---|
| `EBADENGINE` warning | Node < 22 | `nvm install 22 && nvm use 22`. |
| `peer dependency: react` mismatch | React 18 globally pinned | Use `npm install --legacy-peer-deps` (works) or update the global pin. |
| `ResizeObserver is not defined` in Vitest | jsdom missing the polyfill | The test setup at `tests/unit/setup.ts` adds it; make sure your custom config still imports it. |

## Docker compose

| Symptom | Likely cause | Fix |
|---|---|---|
| `502 Bad Gateway` opening playground | jobsvc still starting | `docker compose logs jobsvc`; alembic upgrade can take 30s on first run. |
| `port is already allocated` | something else on 8000/5432/8080 | Edit `docker-compose.yml` to remap, or stop the conflicting service. |
| `cannot create container ... mount denied` | docker socket / SELinux | Add `:Z` to volume mounts on Fedora/RHEL; ensure your user is in the `docker` group. |
| `seed: user already exists` on second start | idempotent — not an error | Ignore. |

## CI / GitHub Pages

| Symptom | Likely cause | Fix |
|---|---|---|
| Pages workflow fails on build | check the [docs CI logs](https://github.com/nimesh08/quantum-stack/actions/workflows/docs.yml) |
| Pages 404 right after enabling | DNS propagation | Wait ~1 minute; refresh. |
| `pymdownx.snippets.SnippetMissingError` | snippet path resolution | The `_includes/` copy step in `docs.yml` materialises the canonical files; if you added a new snippet, add it to that step too. |

## Where to ask

File an issue: <https://github.com/nimesh08/quantum-stack/issues> with
the failing command, full output, and your OS / compiler / Python /
Node versions. The 90% rule: include the **exact stderr** and we can
usually point at the fix in two replies.
