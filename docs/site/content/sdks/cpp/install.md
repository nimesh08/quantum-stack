# C++ SDK — install

## Pre-built binaries (CLIs only)

The simplest path is to install the launcher and pick up `spinorc`,
`phononc`, `photonc-cxx` out of the wheel:

```bash
pip install heisenberg
spinorc --help
phononc --help
photonc-cxx --help
```

This works for users who only need the CLIs. The headers and the
shared libraries are not on PyPI — for those, build from source.

## From source — full engine

### Prerequisites

| Tool | Pin | Where it is documented |
|------|-----|------------------------|
| LLVM / MLIR / Clang | 22.1.8 | [`cmake/Versions.cmake`](https://github.com/nimesh08/quantum-stack/blob/main/cmake/Versions.cmake) |
| CMake | 3.28+ | top-level `CMakeLists.txt` |
| C++ compiler | C++20 | GCC 13 or Clang 17 |
| Eigen | 5.0.1 | pulled by CMake |

LLVM is the biggest dependency. On Debian 13:

```bash
sudo apt install llvm-22 llvm-22-dev mlir-22 clang-22 \
                 libmlir-22-dev cmake ninja-build
```

On macOS arm64:

```bash
brew install llvm@22 cmake ninja eigen
```

Both ship the CMake config files at
`/usr/lib/llvm-22/lib/cmake/` (Linux) or `$(brew --prefix llvm@22)/lib/cmake/`
(macOS).

### Build

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
cmake -B build -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=$(llvm-config-22 --cmakedir) \
  -DMLIR_DIR=$(llvm-config-22 --cmakedir)/../mlir
cmake --build build -j
```

This produces:

- `build/spinor/cli/spinorc`
- `build/phonon/cli/phononc`
- `build/photon/frontends/cpp/photonc-cxx`
- `build/spinor/lib*.a` (and equivalent for phonon/photon)

### Install

```bash
sudo cmake --install build --prefix /usr/local
```

Installs:

- Binaries to `/usr/local/bin/`.
- Headers to `/usr/local/include/spinor/`, `/usr/local/include/phonon/`,
  `/usr/local/include/photon/`.
- Static libraries to `/usr/local/lib/`.
- CMake config files to
  `/usr/local/lib/cmake/{spinor,phonon,photon}/`.

### Verify

```bash
spinorc --version
echo 'target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q' | spinorc parse /dev/stdin
```

### Linking from a downstream project

In a downstream `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app CXX)

find_package(spinor   CONFIG REQUIRED)
find_package(phonon   CONFIG REQUIRED)
find_package(photon   CONFIG REQUIRED)

add_executable(my_app hello.cpp)
target_link_libraries(my_app PRIVATE spinor::spinor)
target_compile_features(my_app PRIVATE cxx_std_20)
```

Then:

```bash
cmake -B build && cmake --build build
```

Heisenberg's CMake config files take care of LLVM/MLIR's transitive
include directories.

---

Heisenberg's C++ SDK was designed and implemented by **Nimesh Cheedella**.
