# C++ SDK — quickstart

Compile the Bell program with `libspinor` from a downstream project,
then submit it from the same C++ binary.

## Project layout

```text
my_app/
├── CMakeLists.txt
└── hello.cpp
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spinor CONFIG REQUIRED)

add_executable(my_app hello.cpp)
target_link_libraries(my_app PRIVATE spinor::spinor)
```

## hello.cpp

```cpp
#include <iostream>
#include <string>

#include <spinor/parser/Parser.h>
#include <spinor/passes/Placement.h>
#include <spinor/passes/Routing.h>
#include <spinor/passes/Decomposition.h>
#include <spinor/passes/Cleanup.h>
#include <spinor/registry/Registry.h>
#include <spinor/emit/Emitters.h>

int main() {
  std::string source =
      "target generic\n"
      "qubit q[2]\n"
      "bit c[2]\n"
      "h q[0]\n"
      "cx q[0], q[1]\n"
      "c = measure q\n";

  auto parsed = spinor::parser::parse(source, "<inline>");
  if (!parsed.module) {
    std::cerr << "parse failed\n";
    return 1;
  }

  spinor::dialect::Diagnostics diag;
  auto reg = spinor::registry::Registry::load("spinor/registry", diag);
  const auto& chip = reg.get("ibm_heron_r2");

  spinor::passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  spinor::passes::Placement pl;
  auto layout = pl.run(*parsed.module, g);

  spinor::passes::Routing routing;
  auto routed = routing.run(*parsed.module, chip, g, layout);

  spinor::passes::Decomposition dec;
  spinor::dialect::Diagnostics decDiag;
  auto decomposed = dec.run(routed.module, chip, decDiag);

  spinor::passes::Cleanup cleanup;
  auto cleaned = cleanup.run(decomposed);

  std::cout << spinor::emit::emitQasm3(cleaned, &chip, {});
}
```

## Build and run

```bash
cmake -B build -GNinja
cmake --build build
./build/my_app
```

You get the chip-locked OpenQASM 3.1 on stdout — the same artefact
`spinorc emit -t ibm_heron_r2 -f qasm3 bell.spn` produces.

## What you have learned

- `find_package(spinor)` pulls every transitive header, including
  LLVM/MLIR's includes.
- The compile pipeline is **parse → place → route → decompose →
  cleanup → emit**, exactly the same five stages `spinorc compile`
  runs.
- The chip registry is a runtime dependency — point it at
  `spinor/registry/` (the source path) or
  `/usr/local/share/spinor/registry/` (the install path).

## Where to next

- [Tutorial](tutorial.md) — embed Photon kernels inside a C++ app
  with `[[photon::kernel]]` and `photonc-cxx`.
- [Cookbook](cookbook.md) — drive `spinorc` from shell, write a
  custom optimizer pass.
- [Reference](../../reference/cpp/index.md) — Doxygen output for
  every public symbol.

---

Heisenberg's C++ SDK was designed and implemented by **Nimesh Cheedella**.
