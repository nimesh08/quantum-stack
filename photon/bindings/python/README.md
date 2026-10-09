# heisenberg-photon

The Photon Python facade, C++ compiler engine, and native command-line tools.

```console
python -m pip install heisenberg-photon
```

The wheel includes:

- the `photon._engine` nanobind extension and Python `kernel`/`QReg` facade;
- the `photonc`, `phononc`, `spinorc`, and `photonc-cxx` native binaries,
  with console launchers installed on PATH;
- all 31 target profiles and topology data;
- the matching qstack runtime as a declared dependency.

## Local execution

Save this example in a Python file so the decorator can read its source:

```python
from photon import QReg, kernel

@kernel(target="ibm_heron_r2")
def bell():
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()

counts = bell.run(shots=1024, mode="local")
print(counts)
```

Local mode invokes the real C++ compiler and simulator. Hardware execution
requires an explicit live mode and provider configuration; historical profiles
are not evidence that a device is currently accessible.

## Building from a checkout

From the repository root:

```console
python -m build --wheel photon/bindings/python
```

The build needs a C++20 compiler and CMake 3.28 or newer. The core compiler does
not require LLVM/MLIR. The build frontend installs the pinned nanobind binding
dependency. Release CI builds and tests Python 3.12/3.13 wheels on supported
Linux, macOS and Windows architectures, including a fresh local Bell execution.

The installed-wheel test is `tests/installed_smoke.py`; run it using the
interpreter into which the wheel and matching runtime were installed.

Licensed under Apache-2.0. The full license is included in the wheel.
