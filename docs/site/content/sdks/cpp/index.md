# C++ SDK

Heisenberg's C++ SDK is the engine itself. Three libraries plus
three CLI binaries:

| Library | What it gives you |
|---------|-------------------|
| `libspinor` | Chip-locking. Placement, routing, decomposition, emit. |
| `libphonon` | Structured IR, linear types, optimizer. |
| `libphoton` | OO front-end + the Clang LibTooling extractor. |

| CLI | What it does |
|-----|--------------|
| `spinorc` | The compiler driver. `parse / verify / compile / emit / check / submit / registry`. |
| `phononc` | Phonon-only entry-point (parses `.pho`-flavoured Phonon, dumps Phonon IR). |
| `photonc-cxx` | C++ front-end (Clang LibTooling extractor + lowering). |

## What is in this section

- [Install](install.md) — pre-built binaries, `cmake --install`,
  full from-source.
- [Quickstart](quickstart.md) — link against `libspinor` from a
  downstream CMake project.
- [Tutorial](tutorial.md) — embed a Photon kernel inside a C++ app.
- [Cookbook](cookbook.md) — emit OpenQASM, drive `spinorc` from
  shell, custom passes.
- [Reference](../../reference/cpp/index.md) — Doxygen-generated API
  docs.

## A complete example

`hello.cpp`:

```cpp
#include <spinor/parser/Parser.h>
#include <spinor/passes/Placement.h>
#include <spinor/registry/Registry.h>
#include <spinor/emit/Emitters.h>

int main() {
  auto src =
      "target generic\n"
      "qubit q[2]\n"
      "bit c[2]\n"
      "h q[0]\n"
      "cx q[0], q[1]\n"
      "c = measure q\n";

  auto parsed = spinor::parser::parse(src, "<inline>");
  if (!parsed.module) return 1;

  spinor::dialect::Diagnostics diag;
  auto reg = spinor::registry::Registry::load("spinor/registry", diag);
  const auto& chip = reg.get("ibm_heron_r2");

  spinor::passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  spinor::passes::Placement pl;
  auto layout = pl.run(*parsed.module, g);

  std::cout << spinor::emit::emitQasm3(*parsed.module, &chip, {});
}
```

Build with `find_package(spinor)` in your `CMakeLists.txt` —
[Quickstart](quickstart.md) walks the full integration.

## API stability

The public C++ headers are documented (Doxygen output is in
[Reference / C++](../../reference/cpp/index.md)). The ABI is
**not yet frozen** — this is a 0.x release. Until 1.0, expect minor
versions to break the C++ ABI. Pin the major version of `libspinor`
and rebuild downstream binaries on each minor bump.

The C++ → Python bridge (`photon._engine` via `nanobind`) **is**
stable across minor versions because the binding presents a smaller
surface than the bare headers.

---

Heisenberg's C++ SDK was designed and implemented by **Nimesh Cheedella**.
