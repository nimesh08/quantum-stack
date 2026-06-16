# `photonc-cxx` CLI

The Phase C C++ frontend driver. Reads a YAML build config, ingests
`[[photon::kernel]]`-marked C++ functions, lowers through Photon →
Phonon → Spinor, prints a resource estimate.

## Install

Built by the top-level CMake build alongside `spinorc`. After
[Install from source](from_source.md):

```
build/photon/frontends/cpp/cli/photonc-cxx
```

The CI build uses the **self-hosted parser** path (no Clang
LibTooling dependency). The LibTooling-backed path lives behind
`#ifdef PHOTON_HAVE_CLANG` and only enables when `find_package(Clang)`
succeeds at configure.

## Build config (YAML)

```yaml
# build.yaml
build:
  sources: [bell.cpp]      # one or more *.cpp files
  entry: bell_kernel       # the [[photon::kernel]]-marked function
  target: generic          # chip id from the registry
  shots: 1024              # passed through to the resource estimate
```

## A minimal kernel

```cpp
// bell.cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
```

## Run

```bash
photonc-cxx build.yaml
# compiled. target=generic shots=1024 num_qubits=2 two_qubit_count=1 depth=4
```

The resource line matches what the
[`jobsvc.engine.compile_program`](../api/python/jobsvc/engine.md#jobsvc.engine.compile_program)
returns — same engine.

## What's the C++ subset?

Intentionally tiny — the ingester rejects anything outside it with a
precise diagnostic. Supported:

- `[[photon::kernel]]` attribute on a function returning `int`.
- `QReg q(N);` declaration.
- Gate methods: `q.h(i)`, `q.cx(c, t)`, `q.rz(angle, i)`, …
- `for (int i = lo; i < hi; ++i) { … }` with constant bounds.
- `if (cond) { … } else { … }`.
- `return q.measure_int();`.

Not supported (and rejected with an error):

- `while`, recursion, dynamic allocation, file I/O.
- Free-function calls except `photon.lib` library routines.

## See also

- [Photon language manual](../languages/photon/index.md).
- [Photon `[[photon::kernel]]` reference](../languages/photon/reference/frontends/cpp_attribute.md).
- [`photonc-cxx` cookbook](../languages/photon/cookbook/bell_three_doors.md).
