# W4 - mid-circuit measurement capability

Measurement leaves a projected state on the original quantum wire. Later gates
and repeated measurements are legal when the selected target supports
mid-circuit measurement. A reset is optional and has different semantics:
it prepares `|0>` while discarding the previous quantum state.

```spinor
target ibm_heron_r2
qubit q[1]
bit c[2]
h q[0]
c[0] = measure q[0]
x q[0]
c[1] = measure q[0]
```

This program records complementary outcomes. The compiler rejects it on a
target whose capability snapshot lacks mid-circuit measurement. It does not
insert resets or move measurements as a capability workaround.

This rule describes an ideal gate-level operation contract. Whether a concrete
readout process realizes it with adequate fidelity requires calibration and
experimental validation.

See [`measure`](../reference/measure.md), [`reset`](../reference/reset.md), and
[reset and reuse](../cookbook/reset_and_reuse.md).
