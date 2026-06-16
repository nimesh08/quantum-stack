# Photon — Phase C

User-facing layer: OO language + `photon.lib`, Python and C++ frontends,
and the nanobind C++<->Python bindings.

## Subfolders

| Folder               | Purpose                                          |
|----------------------|--------------------------------------------------|
| `lang/`              | OO language + `photon.lib`.                      |
| `frontends/python/`  | `@photon.kernel` (ast/inspect).                  |
| `frontends/cpp/`     | Clang LibTooling ingester + `photonc-cxx`.       |
| `bindings/`          | nanobind C++<->Python.                           |
| `tests/`             | Phase C tests (M1..M6).                          |

## Critical rules

- **RULE 1** — Phase B was complete and tested before Phase C started.
- **RULE 3** — One C++ engine. The Python frontend is `inspect.getsource`
  + `ast.parse` + a thin nanobind binding; the C++ frontend is a
  recursive-descent ingester (LibTooling-backed when Clang is found).
  Neither reimplements compilation.

## Status

**Phase C complete.** 45/45 tests pass; the user guide
[`docs/build/phaseC_photon_guide.md`](../docs/build/phaseC_photon_guide.md)
is the entry point. Phase D opens in a fresh chat.
