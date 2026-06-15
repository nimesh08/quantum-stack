# Photon — Phase C

User-facing layer: OO language + `photon.lib`, Python and C++ frontends,
and the nanobind C++<->Python bindings.

## Subfolders

| Folder               | Purpose                                          |
|----------------------|--------------------------------------------------|
| `lang/`              | OO language + `photon.lib`.                      |
| `frontends/python/`  | `@photon.kernel` (ast/inspect).                  |
| `frontends/cpp/`     | Clang LibTooling ingester.                       |
| `bindings/`          | nanobind C++<->Python.                           |
| `tests/`             | Phase C tests.                                   |

## Critical rules that bind this phase

- **RULE 1** — Do not start Phase C until Phase B's tests pass.
- **RULE 3** — One C++ engine. Python here is the thin nanobind binding
  and the `@photon.kernel` decorator only — never a second compiler.

## Status

Empty skeleton. Phase C starts after Phase B is done.
