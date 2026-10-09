# Quantum wire ownership and measurement

The checker rejects stale quantum values reused as independent gate inputs,
duplicate operands, and duplicate function return aliases. These checks prevent
the compiler from treating one quantum wire as two independent wires. They are
compiler invariants; passing them is not a proof of a program's overall physics.

A gate consumes its input SSA value and returns the next value on the same
wire. A helper can return distinct qubit values. Inside a runtime branch, a
returned permutation is materialized as SWAP operations so caller wire
identities agree at the branch join. Substituting freshly allocated wires at
such a join is unsupported and produces an error.

## Measurement preserves a projected quantum state

Computational-basis measurement has outcome maps
`rho -> P_m rho P_m`, where `P_m = |m><m|`. Its classical outcome can control
later operations. The physical qubit still exists in its post-measurement
state; quantum mechanics does not require resetting it before another gate.

On a target that supports mid-circuit measurement, this is valid:

```phonon
target ibm_heron_r2
qubit q[1]
bit c[2]
h q[0]
c[0] = measure q[0]
x q[0]
c[1] = measure q[0]
```

The second outcome is the complement of the first. Inserting `reset q[0]`
before `x` would change that computation. Reset is an explicit operation that
prepares `|0>` and discards the prior state; it is never inserted to repair an
unsupported target capability.

Targets without mid-circuit measurement reject programs requiring it. Actual
readout disturbance and experimental accuracy require hardware evidence beyond
this ideal gate-level contract.

## Supported classical storage

Named `bit` registers are mutable measurement destinations. Numeric scalars
support compile-time calculations. Copying measured data into a numeric scalar
is currently unsupported: use a separate measurement destination for data that
must remain available after another destination is overwritten.

See [measurement capability](../spinor/rules/W4_no_op_after_measure.md),
[`reset`](../spinor/reference/reset.md), and
[post-measurement compilation](cookbook/compile_error_post_measure.md).
