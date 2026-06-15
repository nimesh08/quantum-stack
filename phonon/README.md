# Phonon — Phase B

Chip-independent layer: the dialect that extends Spinor's, lowering, the
linear type checker, and the heavy optimizer.

## Subfolders

| Folder       | Purpose                                                      |
|--------------|--------------------------------------------------------------|
| `dialect/`   | Phonon dialect (extends `spinor/dialect`).                   |
| `lower/`     | Unroll / inline / flatten -> Spinor.                         |
| `types/`     | Linear type checker (no-cloning).                            |
| `optimizer/` | Cancellation, merging, reorder, ZX, synth, schedule.         |
| `tests/`     | Phase B tests.                                               |

## Critical rules that bind this phase

- **RULE 1** — Do not start Phase B until Phase A's tests pass.
- **RULE 2** — The heavy optimizer runs once, above all chips, **here**.

## Status

Empty skeleton. Phase B starts after Phase A is done.
