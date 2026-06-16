# Install from source

Build the entire monorepo from a fresh git clone. This is the path
contributors and Phase A/B/C engine hackers take.

## Prerequisites

| Tool | Version | How to install |
|---|---|---|
| Git | any recent | `apt install git` / `brew install git` |
| CMake | 3.28+ | `apt install cmake` / `brew install cmake` |
| Ninja | any recent | `apt install ninja-build` / `brew install ninja` |
| C++ compiler | GCC 13+ or Clang 17+ | `apt install g++-13` |
| Python | 3.12 floor | `apt install python3.12 python3.12-venv` |
| LLVM/MLIR | **22.1.8** (optional) | see below |

LLVM/MLIR is **optional** — the engine has an in-tree fallback that
builds without it. The MLIR-backed bridge only enables when both
`LLVM_DIR` and `MLIR_DIR` resolve at configure time.

## Clone

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
```

## Configure + build (no LLVM)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

That's enough to build:

- the `spinorc` CLI (Phase A),
- every Phase B optimizer pass,
- the Phase C nanobind binding (when `nanobind` is on `PYTHONPATH`),
- every test under `spinor/tests/`, `phonon/tests/`, `photon/tests/`.

Run the test suite:

```bash
ctest --test-dir build --output-on-failure
```

Expected: **146 ctest entries green** (Phase A 116 + Phase B 17 + Phase C 13).

## Configure + build (with LLVM/MLIR 22.1.8)

If you have LLVM/MLIR 22.1.8 installed:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/opt/llvm-22.1.8/lib/cmake/llvm \
  -DMLIR_DIR=/opt/llvm-22.1.8/lib/cmake/mlir
cmake --build build -j
```

This enables the MLIR-backed bridge — the engine's pass machinery uses
real MLIR ops instead of the in-tree shim. Output identical; just
faster on big circuits.

## Smoke test

```bash
$ ./build/spinor/cli/spinorc registry list
ibm_heron_r2
ionq_aria_proto
ionq_forte
quantinuum_helios

$ echo 'target generic
qubit q[2]
h q[0]
cx q[0], q[1]' > /tmp/bell.spn

$ SPINOR_REGISTRY_ROOT=spinor/registry \
    ./build/spinor/cli/spinorc compile -t ibm_heron_r2 /tmp/bell.spn
```

You should see Spinor IR with native gates (`ecr`, `rz`, `sx`) on
physical qubits.

## What you do next

Now that the engine builds, pick:

- The [`spinorc` CLI](spinorc_cli.md) for command-line use.
- The [`photon` Python package](photon_python.md) for the
  `@photon.kernel` decorator (needs the nanobind binding built).
- The [Docker compose recipe](docker_compose.md) for the whole
  Phase D platform on top.

## Common install errors

See [Troubleshooting](troubleshooting.md).
