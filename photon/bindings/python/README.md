# heisenberg-photon

The Heisenberg Quantum Stack compiler engine, packaged for Python.

```bash
pip install heisenberg-photon
```

## What ships in the wheel

- `photon._engine` — the nanobind C++ extension that drives the
  full Photon → Phonon → Spinor pipeline.
- `photon` — the Python facade with `@kernel`, `QReg`, and
  `compile_phonon`.
- `spinorc` and `photonc` — the everyday CLI binaries, dropped on
  `$PATH` after install. (`phononc` and `photonc-cxx` are
  power-user-only; they ship as signed binaries on GitHub Releases
  for users who want them.)
- The chip registry: `photon/registry/chips/*.yaml` and
  `photon/registry/topologies/*.yaml`. The launcher's
  `find_spinor_registry()` looks for them at
  `Path(photon.__file__).parent / "registry"`.

## Quickstart

```python
from photon import kernel, compile_phonon

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()
```

Or from the shell (the `photonc` binary lands on `$PATH`):

```bash
echo 'target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q' > bell.spn

spinorc compile -t ibm_heron_r2 bell.spn
```

## Where this fits

This package is the **compiler half** of the Heisenberg Quantum Stack.
The platform half — the FastAPI service `jobsvc`, the calibration
scheduler, the playground UI, and the `heisenberg run` launcher —
lives at <https://github.com/nimesh08/heisenberg-platform> and depends
on this wheel from PyPI.

## License

[Apache-2.0](https://github.com/nimesh08/quantum-stack/blob/main/LICENSE).

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.
