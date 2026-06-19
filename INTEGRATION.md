# Integration contract

This repository is the **compiler half** of the Heisenberg Quantum
Stack. The product layer (jobsvc, calibration, launcher, playground,
npm SDK) lives at
[`nimesh08/heisenberg-platform`](https://github.com/nimesh08/heisenberg-platform)
and consumes this repo's compiler artefacts via PyPI.

## Artefacts this repo publishes

### Two PyPI wheels (the contract surface)

| Wheel | Imported as | Provides |
|---|---|---|
| **`heisenberg-photon`**        | `import photon`        | `photon._engine` (the C++ engine via nanobind), `@photon.kernel`, `compile_phonon`, the chip registry, and the `spinorc` + `photonc` CLI binaries on `$PATH` |
| **`heisenberg-spinor-submit`** | `import spinor_submit` | `spinor_submit.submit(qasm, chip, provider, shots)`; cassette + live modes |

```bash
pip install heisenberg-photon heisenberg-spinor-submit
```

After install, `spinorc --help` and `photonc --help` work — the
binaries ride inside `heisenberg-photon` as package data and are
console-scripts on `$PATH`.

### Four signed CLI binaries on GitHub Releases (optional)

For users who do not want a Python venv, every tagged release
attaches signed binaries:

| Binary | What it is |
|---|---|
| `spinorc`     | Spinor compiler driver (chip-locking; emits QASM3/QIR/Quil). |
| `phononc`     | Phonon driver (lowers `.phn` -> Spinor; shells out to `spinorc`). |
| `photonc`     | Photon language driver — handles `.pho` source files. |
| `photonc-cxx` | Clang LibTooling-based ingester for C++ files containing `[[photon::kernel]]`. |

Signed with `cosign` keyless (Sigstore) and accompanied by a
CycloneDX SBOM produced by `syft`. See the latest release at
<https://github.com/nimesh08/quantum-stack/releases>.

### Three internal C++ libraries (`cmake --install` only)

For C++ consumers who embed Heisenberg directly:

- `libspinor` + `spinor/*.h`
- `libphonon` + `phonon/*.h`
- `libphoton` + `photon/*.h`

The C++ ABI is **not yet frozen** (we are pre-1.0). Pin the major
version of any C++ binary you build against and rebuild on minor
bumps.

## Version policy

We follow [Semantic Versioning](https://semver.org/). Concretely:

- **Patch (0.5.x -> 0.5.x+1)**: bug fixes; wheel-compatible.
- **Minor (0.5.x -> 0.6.0)**: new features; wheel-compatible (the
  Python ABI surface stays stable). The C++ ABI may change.
- **Major (0.x -> 1.0)**: only when we declare the public C++ ABI
  stable. Until then, every minor bump may break C++ ABI.

The platform repo's `pyproject.toml` files pin the compatibility
window via `>=0.5, <0.6`. The lower bound is the minimum tested;
the upper bound is the major.minor of the compiler the platform was
built for. Bumping it is a deliberate PR.

## How to upgrade if you depend on this repo

```bash
# Direct PyPI consumer:
pip install --upgrade heisenberg-photon heisenberg-spinor-submit

# As a downstream Python project:
# Edit your pyproject.toml.
#   heisenberg-photon          >=0.5, <0.6   -->   >=0.6, <0.7
#   heisenberg-spinor-submit   >=0.5, <0.6   -->   >=0.6, <0.7
```

## Distribution channels

| Channel | URL |
|---|---|
| PyPI                | <https://pypi.org/project/heisenberg-photon/> |
| PyPI (adapters)     | <https://pypi.org/project/heisenberg-spinor-submit/> |
| TestPyPI (dry runs) | <https://test.pypi.org/project/heisenberg-photon/> |
| GitHub Releases     | <https://github.com/nimesh08/quantum-stack/releases> |
| Documentation       | <https://nimesh08.github.io/quantum-stack/> |
| Source              | <https://github.com/nimesh08/quantum-stack> |

## Notes for downstream consumers

- The chip registry ships **inside** `heisenberg-photon`; downstream
  Python code should locate it via
  `Path(photon.__file__).parent / "registry"`. The platform repo's
  launcher does exactly this.
- Provider SDKs (qiskit-ibm-runtime, amazon-braket-sdk,
  azure-quantum) are **optional extras** of `heisenberg-spinor-submit`,
  installed via:
  ```bash
  pip install 'heisenberg-spinor-submit[ibm]'    # qiskit + qiskit-ibm-runtime
  pip install 'heisenberg-spinor-submit[aws]'    # amazon-braket-sdk
  pip install 'heisenberg-spinor-submit[azure]'  # azure-quantum
  ```
- Cassette mode (`SPINOR_SUBMIT_MODE=cassette`) requires no provider
  SDKs at all.

## RULE 7 — Trademark gate

The names *Photon*, *Phonon*, and *Spinor* are working names. Until a
trademark search clears, we publish PyPI packages under the
`heisenberg-*` prefixed namespace. Bare names (`photon`, `phonon`,
`spinor`) on PyPI are not used by this project.
