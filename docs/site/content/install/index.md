# Install

Pick the layer you want and follow that page. Each install is independent
— for example, you can use the `spinorc` CLI without ever touching the
playground, or run `jobsvc` without the calibration scheduler.

The fastest way to get everything is the [Docker compose recipe](docker_compose.md).

## Pick your starting point

| You want to… | Read |
|---|---|
| **Run on a production server** (recommended) | [Server (systemd)](server_systemd.md) |
| Bring up the full stack on your laptop in 60 seconds | [Docker compose](docker_compose.md) |
| Build the C++ engine from source | [From source](from_source.md) |
| Compile `.spn` files at the command line | [`spinorc` CLI](spinorc_cli.md) |
| Compile Photon `.cpp` kernels at the command line | [`photonc-cxx` CLI](photonc_cxx.md) |
| Use `@photon.kernel` from Python | [`photon` Python package](photon_python.md) |
| Submit OpenQASM 3 to IBM/AWS/Azure from Python | [`spinor_submit` package](spinor_submit.md) |
| Run the FastAPI job service yourself | [`jobsvc`](jobsvc.md) |
| Run the nightly calibration refresh | [`calibration`](calibration.md) |
| Build / customise the Playground SPA | [Playground](playground.md) |
| Hit a snag during install | [Troubleshooting](troubleshooting.md) |

## Pinned versions (re-verified 2026-06-16)

| Component | Pin | Why this version |
|---|---|---|
| LLVM / Clang / MLIR | 22.1.8 | engine; final 22.1.x |
| C++ | C++20 | engine + frontends |
| CPython | 3.13 target / 3.12 floor | `@photon.kernel` decorator |
| nanobind | 2.12.0 | C++↔Python binding |
| FastAPI | 0.137.1 | job service API |
| PostgreSQL | 17.10 | jobs / results storage |
| React | 19.2.7 | playground SPA |
| @monaco-editor/react | ^4.7.0 | first stable with React 19 |
| Node | 22.x | playground build |
| Docker engine | 25+ | compose v2 |

Authoritative pins live in
[`cmake/Versions.cmake`](https://github.com/nimesh08/quantum-stack/blob/main/cmake/Versions.cmake),
[`platform/jobsvc/pyproject.toml`](https://github.com/nimesh08/quantum-stack/blob/main/platform/jobsvc/pyproject.toml),
and [`platform/playground/package.json`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/package.json).
