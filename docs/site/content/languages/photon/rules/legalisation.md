# Legalisation — chip-specific lowering of Photon kernels

When a Photon kernel is compiled for a specific chip, the legalisation
pass adapts it to the chip's capabilities (`supports.*` in the YAML).

## What gets adapted

### `mid_circuit_measure`

If the kernel calls `q.measure(i)` and then more gates on `q[i]`:

- Chip with `supports.mid_circuit_measure: true` → legal as written.
- Chip without → the legalisation pass inserts a `q.reset(i)` if the
  chip supports reset, otherwise rejects with a precise error.

### `feedforward`

If the kernel has `if (m[0] == 1) { q.x(t) }`:

- Chip with `supports.feedforward: true` → emit feedforward.
- Chip with `"limited"` → emit if the body is a single gate and the
  condition is `cbit == 0|1`; otherwise reject.
- Chip with `false` → fall back to **post-selection** (run all
  branches, discard non-matching shots).

See the Phonon-side rule pages:
[feedforward_legalisation](../../phonon/rules/feedforward_legalisation.md),
[post_selection](../../phonon/rules/post_selection.md).

### `reset`

If the kernel calls `q.reset(i)`:

- `supports.reset: true` → legal.
- otherwise → reject (chip can't physically reset; you have to write
  the program differently).

## Diagnostic

```
error: kernel 'foo' uses mid-circuit measurement on chip 'ionq_aria_proto'
       which has supports.mid_circuit_measure: false; rewrite to
       defer all measurements to the end, or pick a chip that supports it.
```

## See also

[Phonon legalisation](../../phonon/rules/feedforward_legalisation.md),
[`q.reset`](../reference/methods/measure_measure_int_reset.md)
