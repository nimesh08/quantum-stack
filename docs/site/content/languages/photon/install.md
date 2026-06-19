# Photon — install

Photon ships in three flavours, one per frontend.

## Python frontend (`@photon.kernel`)

```bash
pip install heisenberg-photon
```

That gives you the `photon` Python package — the `@kernel`
decorator, `QReg`, `compile()`, `run()`, and the `photon.lib`
standard library. It depends on the C++ engine via a `nanobind`
binding (`photon._engine`); the binding is bundled in the wheel for
Linux x86_64 and macOS arm64.

Verify:

```python
from photon import kernel

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()

print(bell)        # prints the lowered Phonon IR
```

## `.pho` source files

Compiled by the same `spinorc` binary that handles Spinor and
Phonon. Install via the launcher:

```bash
pip install heisenberg
spinorc compile -t ibm_heron_r2 my_kernel.pho
```

See [Spinor / install](../spinor/install.md) for the full path
including from-source builds.

## C++ frontend (`[[photon::kernel]]`)

Only needed if you embed Photon kernels in C++ application code.
Built from source as part of the C++ engine:

```bash
git clone https://github.com/nimesh08/quantum-stack.git
cd quantum-stack
cmake -B build -GNinja
cmake --build build --target photonc-cxx
./build/photon/frontends/cpp/photonc-cxx --help
```

`photonc-cxx` uses Clang LibTooling at build time to extract Photon
kernels from your C++ source; the extracted kernels are lowered
through Phonon and Spinor, and the result is linked against your
binary as either a QASM3 string constant or an executable artefact.

## Verify any door

The same trivial kernel runs on every door. Pick the one for your
language and you should see `{"00": ~500, "11": ~500}`.

```bash
python -c "from photon import kernel, run; \
  @kernel
  def b():
      q = QReg(2); q.h(0); q.cx(0,1); return q.measure()
  print(run(b, target='generic', shots=1000, mode='cassette'))"
```

---

!!! quote "Credits"
    **Photon** was designed and implemented by **Nimesh Cheedella**.
