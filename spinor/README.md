# Spinor — Phase A

Per-chip layer of the quantum compiler stack.

## Subfolders

| Folder      | Purpose                                                   |
|-------------|-----------------------------------------------------------|
| `dialect/`  | Spinor MLIR dialect (TableGen).                           |
| `parser/`   | Lark prototype (Python, throwaway), then C++ recursive descent. |
| `passes/`   | Placement, routing (SABRE), decomposition, tiny cleanup.  |
| `registry/` | Per-chip YAML + loader + schema.                          |
| `sim/`      | Stim wrapper + statevector engine.                        |
| `emit/`     | OpenQASM 3 / QIR / Quil emitters.                         |
| `submit/`   | Provider adapters behind one interface (verbatim only).   |
| `tests/`    | Phase A tests.                                            |

## Critical rules that bind this phase

- **RULE 2** — Optimization lives in **Phonon**, never in Spinor. Spinor only
  specializes a program to one chip. If a circuit-shrinking optimizer is
  appearing here, stop: that is an architecture error.
- **RULE 3** — One C++ engine. Python in this folder is allowed in exactly
  one place: `parser/` as a throwaway Lark prototype, replaced by
  hand-written C++ before Phase A is done.
- **RULE 5** — Submission to providers is verbatim / pass-through only.

## Status

Empty skeleton. Phase A implementation has not started.
