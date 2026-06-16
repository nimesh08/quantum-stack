# Feedforward legalisation

`if` after `measure` is **feedforward** — the chip needs to
classically condition a subsequent gate on a measurement result.
Not every chip can do this.

## Three legalisation paths

The legalisation pass reads `chip.supports.feedforward` from the YAML:

| `supports.feedforward` | Behaviour |
|---|---|
| `true` (e.g. Quantinuum) | emit feedforward directly; native execution |
| `"limited"` (e.g. IBM) | emit only simple equality conditions; reject complex ones with a precise error |
| `false` (e.g. IonQ) | rewrite to **post-selection** — see [post_selection](post_selection.md) |
| absent / unknown | reject with a precise error |

## What "limited" means on IBM

IBM's runtime supports `if (cbit == 0|1) { single_gate }` patterns but
not arbitrary nested conditions. The legalisation pass rejects:

```phonon
if (m[0] == 1 && m[1] == 0) {  ; ERROR on limited chips
    x q[0]
    z q[0]                     ; multiple gates — not OK either
}
```

…and accepts:

```phonon
if (m[0] == 1) { x q[0] }      ; OK
```

## Diagnostic

```
error: feedforward not supported on chip 'ionq_forte';
       legalisation will fall back to post-selection
note: this discards shots whose measurement doesn't match;
      effective shot count drops by ~50% per feedforward branch
```

## See also

[`if`](../reference/if.md), [post_selection](post_selection.md),
[Cookbook: teleport](../cookbook/teleport.md)
