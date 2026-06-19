# AUTHORS

Heisenberg, Spinor, Phonon and Photon were designed and implemented by:

| Author | Role | Contact |
|--------|------|---------|
| **Nimesh Cheedella** | Author and maintainer of the entire stack — compiler engine, languages, platform, launcher, documentation. | <chnimesh0808@gmail.com> · [github.com/nimesh08](https://github.com/nimesh08) |

## What this means

- The Spinor C++ compiler engine, the Phonon IR + linear type
  checker + optimizer, the Photon OO front-end with three doors
  (`@photon.kernel`, `[[photon::kernel]]`, `.pho`), and the
  platform layer (jobsvc, worker, calibration, playground,
  launcher) — all of it.
- The chip registry, the provider adapters, the documentation site
  — all of it.

## Contributing

Heisenberg is open-source under
[Apache-2.0](https://github.com/nimesh08/quantum-stack/blob/main/LICENSE).
Pull requests are welcome — see
[`CONTRIBUTING.md`](https://github.com/nimesh08/quantum-stack/blob/main/CONTRIBUTING.md)
for the dev setup and the PR conventions.

When your patch lands, your name and a one-line description of the
contribution will be added below.

## Contributors

*(none yet — be the first!)*

## Acknowledgements

- The LLVM / MLIR project, on which the compiler infrastructure is
  built.
- The FastAPI, SQLAlchemy, React, and TanStack Query communities,
  whose libraries shape the platform layer.
- Every quantum-hardware vendor whose published gate sets and
  topologies make this stack useful.
