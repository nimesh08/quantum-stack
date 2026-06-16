# Post-selection — when feedforward isn't supported

If your chip can't classically condition gates on measurements, the
legalisation pass rewrites the program to **run all branches** and
**keep only the shots where the measurement matched**.

## How it works

Original (feedforward):

```phonon
m = measure src
if (m[0] == 1) { x dst }
```

Rewritten (post-selection):

```phonon
; Run BOTH branches; tag the shot with the measurement result.
m = measure src
x dst                        ; always apply the correction
; (At the end, the platform discards shots where m[0] != 1.)
```

The legalisation pass adds a metadata tag to the program; the
post-processing pass on the histogram drops the bad shots.

## Cost

Effective shot count drops by **2^k** per feedforward branch, where
k is the number of conditional branches. For one branch, you keep
~50% of shots.

To compensate, request `2 × shots` and accept ~half are discarded.

## When you need to know

You don't, normally. The legalisation pass picks the best path
automatically. The diagnostic when post-selection is unavoidable:

```
note: chip 'ionq_forte' does not support feedforward; using
      post-selection (effective shots: ~50%)
```

If your shot budget is tight, switch to a chip with
`supports.feedforward: true`.

## See also

[`if`](../reference/if.md), [feedforward_legalisation](feedforward_legalisation.md)
