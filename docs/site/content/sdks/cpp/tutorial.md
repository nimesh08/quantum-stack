# C++ SDK — tutorial

We will embed a Photon kernel inside a C++ application using the
`[[photon::kernel]]` attribute and `photonc-cxx`. This is the same
flow that powers the C++ door of Photon — see
[Languages / Photon / Tutorial](../../languages/photon/tutorial.md)
for the full three-door comparison.

## Project layout

```text
my_app/
├── CMakeLists.txt
├── main.cpp
└── kernels/
    └── bell.cpp
```

## kernels/bell.cpp

```cpp
#include <photon/lib.hpp>

[[photon::kernel]]
photon::bit<2> bell() {
  photon::QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure();
}
```

`[[photon::kernel]]` is a marker attribute. The C++ compiler ignores
it (it is harmless metadata); `photonc-cxx` reads it via Clang
LibTooling, extracts the kernel, lowers it through Phonon and
Spinor, and emits a chip-locked QASM3 string.

## Compile the kernel

```bash
photonc-cxx --target ibm_heron_r2 --emit qasm3 \
            kernels/bell.cpp -o kernels/bell.qasm3
```

Inspect the result:

```bash
cat kernels/bell.qasm3
```

You see the chip-locked OpenQASM 3.1 wrapped in
`#pragma braket verbatim`.

## Embed the artefact

In `main.cpp`, embed the QASM3 string and submit it via the
`spinor_submit` Python adapter:

```cpp
#include <iostream>
#include <string>

extern const char* kBellQasm;     // populated by `xxd -i kernels/bell.qasm3`

int main() {
  std::cout << "Bell program (chip-locked):\n" << kBellQasm << "\n";
  // To submit at runtime, shell out to `spinorc submit -t ibm_heron_r2`.
  return 0;
}
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(photon CONFIG REQUIRED)

# Generate the chip-locked QASM3 from the Photon kernel at build time.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/bell.qasm3
  COMMAND photonc-cxx --target ibm_heron_r2 --emit qasm3
          ${CMAKE_SOURCE_DIR}/kernels/bell.cpp
          -o ${CMAKE_BINARY_DIR}/bell.qasm3
  DEPENDS kernels/bell.cpp
)

# Embed it into the binary.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/bell_qasm.c
  COMMAND xxd -i bell.qasm3 ${CMAKE_BINARY_DIR}/bell_qasm.c
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS ${CMAKE_BINARY_DIR}/bell.qasm3
)

add_executable(my_app
  main.cpp
  ${CMAKE_BINARY_DIR}/bell_qasm.c
)
target_link_libraries(my_app PRIVATE photon::photon)
```

Build:

```bash
cmake -B build -GNinja
cmake --build build
./build/my_app
```

## What you have learned

- `[[photon::kernel]]` is the C++ attribute that marks a function
  as a quantum kernel.
- `photonc-cxx` is the Clang-based front-end that extracts and
  lowers Photon kernels at build time.
- The output is a chip-locked QASM3 (or QIR / Quil) string you can
  embed in your binary.
- The same kernel compiled via `@photon.kernel` (Python) or `.pho`
  source produces *byte-identical* QASM3 — RULE 3 in action.

## Where to next

- [Cookbook](cookbook.md) — drive `spinorc` from shell, write a
  custom optimizer pass, intercept the IR after each stage.
- [Languages / Photon](../../languages/photon/index.md) — the
  language reference.
- [Reference / C++](../../reference/cpp/index.md) — Doxygen API.

---

Heisenberg's C++ SDK was designed and implemented by **Nimesh Cheedella**.
