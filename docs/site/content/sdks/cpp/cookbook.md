# C++ SDK — cookbook

Recipes for downstream C++ projects.

## Drive `spinorc` from a Makefile

```makefile
QASM := build/bell.qasm3

$(QASM): kernels/bell.spn
	mkdir -p $(@D)
	spinorc emit -t ibm_heron_r2 -f qasm3 --verbatim $< > $@

submit: $(QASM)
	spinorc submit -t ibm_heron_r2 --provider ibm \
	               --mode cassette --shots 1000 $<
```

## Read a chip YAML at runtime

```cpp
#include <spinor/registry/Registry.h>
#include <iostream>

int main() {
  spinor::dialect::Diagnostics diag;
  auto reg = spinor::registry::Registry::load(
      std::getenv("SPINOR_REGISTRY_ROOT"), diag);
  for (const auto& id : reg.ids()) {
    const auto& chip = reg.get(id);
    std::cout << id << ": " << chip.qubits << " qubits, "
              << chip.nativeGates.size() << " native gates\n";
  }
}
```

## Add a new optimizer pass

Phonon optimisations live in
[`phonon/optimizer/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon/optimizer).
A new pass is a class deriving from `phonon::optimizer::Pass`:

```cpp
namespace phonon::optimizer {

class MyPass : public Pass {
 public:
  std::string name() const override { return "my-pass"; }
  Module run(const Module& m) override {
    Module out = m;
    // ... rewrite ops on `out` ...
    return out;
  }
};

}  // namespace phonon::optimizer
```

Register it in
[`phonon/optimizer/lib/Optimizer.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/phonon/optimizer/lib/Optimizer.cpp).
Then `spinorc compile --pass my-pass <file>` runs it.

## Intercept the IR after each stage

```bash
spinorc compile -t ibm_heron_r2 bell.spn \
                --dump phonon-after-opt \
                --dump spinor-after-decompose
```

Both dumps go to stderr in plain text; pipe to a file with
`2>dump.txt`.

## Write a small custom emitter

```cpp
#include <spinor/dialect/Spinor.h>
#include <spinor/registry/Registry.h>

namespace my_emit {

std::string emit(const spinor::dialect::Module& m,
                 const spinor::registry::ChipInfo&) {
  std::string out;
  for (const auto& op : m.ops()) {
    // your custom serialisation here
  }
  return out;
}

}  // namespace my_emit
```

Wire it into your downstream binary; do not modify
`spinor/emit/lib/` in-tree unless you intend to upstream the change.

## Build with sanitizers

```bash
cmake -B build-asan -GNinja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan
```

Heisenberg's C++ test suite is ASan/UBSan-clean upstream; if you
hit a leak or UB in an SDK consumer, please open an issue.

---

Heisenberg's C++ SDK was designed and implemented by **Nimesh Cheedella**.
