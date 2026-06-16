# `[[photon::kernel]]` — C++ frontend  *[frontend]*

C++ attribute. Marks a function as a Photon kernel; the
`photonc-cxx` driver (Clang LibTooling or self-hosted) ingests it.

## Synopsis

```cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
```

## Build YAML

```yaml
build:
  sources: [bell.cpp]
  entry: bell_kernel
  target: generic
  shots: 1024
```

## Drive

```bash
photonc-cxx build.yaml
# compiled. target=generic shots=1024 num_qubits=2 two_qubit_count=1 depth=4
```

## What's accepted

The same OO subset as `.pho`:

- `QReg q(N);` declaration
- gate methods: `q.h(i)`, `q.cx(c, t)`, `q.rz(angle, i)`, ...
- `for (int i = lo; i < hi; ++i) { … }` with constant bounds
- `if (cond) { … } else { … }`
- `return q.measure_int();`

## What's rejected

- `while`, recursion, dynamic allocation, file I/O, `printf`.
- Free-function calls except `photon.lib`.

## Two paths

The default CI path is **self-hosted**: a small recursive-descent
parser for the Photon C++ subset. The LibTooling path lives behind
`#ifdef PHOTON_HAVE_CLANG` and only enables when Clang is present at
configure time. Both produce the same `photon::lang::Module`.

## Source

[`photon/frontends/cpp/lib/Ingest.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/cpp/lib/Ingest.cpp),
[`photon/frontends/cpp/cli/Photonc.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/cpp/cli/Photonc.cpp).

## See also

[`pho_file`](pho_file.md), [`@photon.kernel`](photon_kernel.md),
[Install: photonc-cxx](../../../../install/photonc_cxx.md)
