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

The compiler checks the concrete target capability snapshot and emits a real
classically controlled gate. Targets without required feedforward support
receive a compilation error. There is [no automatic postselection](../rules/post_selection.md).

## Variations

- **Multiple conditions**: separate `if` blocks, each one simple.
- **Else branch**: `if (m[0] == 1) { ... } else { ... }` preserves both arms.
  If a target cannot represent the program, compilation rejects it.

## Same in Photon

```photon
QReg q(2)
q.h(0)
mid = q.measure(0)
if (mid[0] == 1) { q.x(1) }
```

## Side effects on cost

Classical-control latency depends on the concrete target and its timing
metadata. Missing timing data is reported as unavailable; no fixed latency or
shot reduction is assumed.

## Where to look

- Reference: [`if`](../reference/if.md), [`measure`](../../spinor/reference/measure.md)
- Rules: [feedforward_legalisation](../rules/feedforward_legalisation.md)
