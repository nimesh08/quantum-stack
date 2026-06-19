# Spinor — install

Spinor is the C++ compiler engine. The CLI is called `spinorc`.

## With `heisenberg`

The simplest path: install the launcher and let it pull a pre-built
`spinorc` for your platform.

```bash
pip install heisenberg
heisenberg version              # confirms spinorc is wired
```

This installs Heisenberg's signed `spinorc` binary into the same
virtual-env as `heisenberg`. After this, `spinorc --help` works.

(The binary is not yet on every distro's package manager; the
`pip install` path is the supported flow until Linux distro
packages land.)

## From source

If you are contributing to the compiler, build it locally.

### Prerequisites

| Tool | Pin | Notes |
|------|-----|-------|
| LLVM / MLIR / Clang | 22.1.8 | The full pin is in [`cmake/Versions.cmake`](https://github.com/nimesh08/quantum-stack/blob/main/cmake/Versions.cmake). |
| CMake | 3.28 or later | |
| C++ compiler | C++20 | GCC 13 or Clang 17 |
| Eigen | 5.0.1 | Pulled by CMake |
| Python | 3.13 (3.12 floor) | Only needed for `heisenberg` and the Python frontend |

### Build

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
cmake -B build -GNinja \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/path/to/llvm/lib/cmake/mlir
cmake --build build -j
```

This produces `build/spinor/cli/spinorc`. Either add it to your
`PATH` or `cmake --install build --prefix ~/.local`.

### Verify

```bash
spinorc --help
spinorc registry list           # prints all 27 known chip ids
echo 'target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q' > /tmp/bell.spn
spinorc verify -t ibm_heron_r2 /tmp/bell.spn
spinorc compile -t ibm_heron_r2 /tmp/bell.spn
```

The last command emits the chip-locked program to stdout.

## Subcommands you will use

| Command | What it does |
|---------|--------------|
| `spinorc parse <file>` | Parse and pretty-print the IR. |
| `spinorc verify -t <chip> <file>` | Parse + run the W1-W7 verifier. |
| `spinorc compile -t <chip> <file>` | Place + route + decompose + cleanup. |
| `spinorc emit -t <chip> -f <qasm3 \| qir \| quil> <file>` | Compile and emit to wire format. |
| `spinorc check -t <chip> <file>` | Compile and print resource estimate plus equivalence check (when chip ≤ 12 qubits). |
| `spinorc submit -t <chip> [--provider P] [--mode m] [--shots N] <file.qasm3>` | Submit (live, cassette, or local). |
| `spinorc registry list` | List every chip in the registry. |

## Where the binary lives after install

| Layer | Path |
|-------|------|
| `heisenberg` (PyPI) | `<your-venv>/bin/spinorc` |
| `cmake --install` | `<prefix>/bin/spinorc` |
| Source build, no install | `build/spinor/cli/spinorc` |

You can override the registry root with `SPINOR_REGISTRY_ROOT` if
you want to point at a custom chip set.

---

!!! quote "Credits"
    **Spinor** was designed and implemented by **Nimesh Cheedella**.
