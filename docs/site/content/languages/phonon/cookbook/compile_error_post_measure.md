# Gate after measurement

A gate after measurement is mathematically valid, but the selected device must
support mid-circuit measurement. For example:

```phonon
target ibm_heron_r2
qubit q[1]
bit c[2]
h q[0]
c[0] = measure q[0]
x q[0]
c[1] = measure q[0]
```

The two results are complementary. No reset is required by the ideal circuit
semantics. If the target does not support this sequence, compilation rejects
it with a capability diagnostic. Select a compatible target, or revise the
algorithm and verify its semantics separately.

Do not insert a reset merely to silence an error: resetting before `x` changes
the second outcome to one regardless of the first. Similarly, moving a
measurement changes the channel in general and requires justification.

See [measurement capability](../../spinor/rules/W4_no_op_after_measure.md) and
[`reset`](../../spinor/reference/reset.md).
