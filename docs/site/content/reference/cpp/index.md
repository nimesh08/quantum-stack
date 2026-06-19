# Reference — C++

Doxygen-generated reference for the C++ engine.

The full HTML embed is below. If you are reading this in the source
tree before the docs CI has run, see
[`docs/cpp/Doxyfile`](https://github.com/nimesh08/quantum-stack/blob/main/docs/cpp/Doxyfile)
for the generator config.

<iframe
  src="../../assets/cpp/index.html"
  style="width:100%;height:80vh;border:0"
  loading="lazy"></iframe>

If the iframe is empty, the Doxygen build has not run yet for this
revision; see [Operations / Troubleshooting](../../operations/troubleshooting.md).

## Where the headers live

| Library | Headers |
|---------|---------|
| `libspinor` | [`spinor/dialect/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/dialect/include), [`parser/cpp/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/parser/cpp/include), [`passes/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/passes/include), [`registry/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/include), [`emit/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/emit/include), [`sim/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/sim/include), [`submit/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/submit/include), [`verify/include/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/verify/include) |
| `libphonon` | [`phonon/dialect/include/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon/dialect/include), [`lower/include/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon/lower/include), [`types/include/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon/types/include), [`optimizer/include/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon/optimizer/include) |
| `libphoton` | [`photon/lang/include/`](https://github.com/nimesh08/quantum-stack/tree/main/photon/lang/include), [`bindings/include/`](https://github.com/nimesh08/quantum-stack/tree/main/photon/bindings/include) |

---

Heisenberg's C++ API was authored by **Nimesh Cheedella**.
