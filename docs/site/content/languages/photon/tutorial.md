# Photon — tutorial

We will write a Bell program **three times**, once through each
front-end, and confirm all three produce identical Phonon IR. This
is what RULE 3 ("one C++ engine, one source of truth") looks like in
practice.

## What we are building

The same six-line Bell pair you saw in
[Spinor / tutorial](../spinor/tutorial.md), but expressed in
Photon's OO surface — `QReg(2)` instead of `qubit q[2]`, methods
instead of bare gates.

## Door 1 — `.pho` source

Save as `bell.pho`:

```photon
target generic

kernel bell() -> bit[2] {
  let q = QReg(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure();
}
```

Compile and run:

```bash
spinorc compile -t generic bell.pho > bell.spinor
spinorc emit -t ibm_heron_r2 -f qasm3 --verbatim bell.pho > bell.qasm3
```

## Door 2 — Python decorator

```python
from photon import kernel, compile, run

@kernel
def bell() -> bit[2]:
    q = QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure()

artefact = compile(bell, target="ibm_heron_r2")
hist = run(artefact, shots=1000, mode="cassette")
print(hist)        # {"00": ~500, "11": ~500}
```

The decorator does not interpret the Python at runtime. It uses
`inspect.getsource` plus `ast.parse` to build a Photon AST, then
hands it to the C++ engine. Subset of Python is supported (no
external function calls, no list comprehensions, no closures over
non-quantum state); the
[unsupported-constructs page](rules/unsupported_constructs.md)
lists the exact rejection rules.

## Door 3 — C++ attribute

In `bell.cpp`:

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

Compile through `photonc-cxx`:

```bash
photonc-cxx --target ibm_heron_r2 bell.cpp -o bell.qasm3
```

`photonc-cxx` runs Clang LibTooling against your source, extracts
kernels marked with `[[photon::kernel]]`, lowers each one through
Phonon and Spinor, and writes the result.

## Verify three-door convergence

```bash
spinorc dump --phonon bell.pho                                        > a.phonon
python -c 'from photon import dump_phonon; \
           import bell; print(dump_phonon(bell.bell))'                > b.phonon
photonc-cxx --dump phonon bell.cpp                                    > c.phonon

diff a.phonon b.phonon       # empty
diff b.phonon c.phonon       # empty
```

All three doors produce the same Phonon IR. From there it is a
single Spinor pipeline to the chip.

## What you have learned

- Photon is an OO surface over Phonon. `QReg(n)` is the quantum
  register; methods on it are gates.
- Three front-ends — `.pho`, `@photon.kernel`, `[[photon::kernel]]`
  — converge on the **same** Phonon IR.
- The Python decorator is `inspect.getsource` + `ast.parse` + a
  `nanobind` call into the C++ engine. It does not reimplement
  compilation.
- The standard library `photon.lib` ships famous algorithms
  (`bell_pair`, `ghz`, `qft`, `grover`, `teleport`, `vqe_ansatz`)
  ready to call.

## Where to next

- The [Cookbook](cookbook/index.md) — Grover, QFT, teleport, VQE.
- The [Python SDK](../../sdks/python/index.md) — how `compile()`
  and `run()` are wired.
- Move down: [Phonon](../phonon/index.md) is what `@photon.kernel`
  produces, and what the optimizer rewrites.

---

!!! quote "Credits"
    **Photon** was designed and implemented by **Nimesh Cheedella**.
