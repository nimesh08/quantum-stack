# Classical branching — `measure` → `int` → `if`

## What it does

Use a measurement result to drive subsequent gates. The "feedforward"
pattern that makes Phonon different from Spinor.

## Recipe

```phonon
target quantinuum_helios     ; supports full feedforward

qubit q[2]
bit  m[1]

h q[0]
m = measure q[0]
if (m[0] == 1) {
    x q[1]                   ; classically-controlled X
}
```

## Why it works

The legalisation pass reads `supports.feedforward: true` from the
chip YAML and emits a real classical-controlled gate. On chips
without feedforward, the same code post-selects (see
[post_selection](../rules/post_selection.md)).

## Variations

- **Multiple conditions**: separate `if` blocks, each one simple.
- **Else branch**: `if (m[0] == 1) { ... } else { ... }`. On limited
  chips (IBM "limited"), only the `if` body is emitted; the `else`
  becomes the no-op default.

## Same in Photon

```photon
QReg q(2)
q.h(0)
mid = q.measure(0)
if (mid[0] == 1) { q.x(1) }
```

## Side effects on cost

A feedforward branch on a feedforward-supporting chip is essentially
free in time but adds latency for the classical control. On a chip
without feedforward, post-selection halves the effective shot count
per branch.

## Where to look

- Reference: [`if`](../reference/if.md), [`measure`](../../spinor/reference/measure.md)
- Rules: [feedforward_legalisation](../rules/feedforward_legalisation.md)
