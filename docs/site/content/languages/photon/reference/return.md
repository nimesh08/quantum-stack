# `return` — kernel result

Photon and Python kernel returns keep one fixed result type and width across
reachable paths. A return exits the kernel and suppresses later statements.

Supported results are measured registers (`measure` or `measure_int`, with the
same representation across paths), Boolean controller values, and explicit
fixed-width unsigned controller values. Equal-width measurements from different
registers share one return destination. Internal saved flags and compiler
storage are excluded from the public kernel return.

```python
@photon.kernel
def choose():
    q = photon.QReg(1)
    measured = q.measure()
    if measured[0] == 1:
        return photon.uint(8, 7)
    return photon.uint(8, 3)
```

Bounded-loop early returns clear active execution paths. A value-returning
kernel must also provide a compatible result on paths that leave the loop
without returning. Exhausted shots retain their status and data; Python
`kernel.run()` raises `PhotonKernelError` with the saved full result in
`error.result` rather than returning an apparently successful histogram.

Arbitrary classical helper-call expressions are not part of the implemented
language. Phonon helpers retain their quantum input/output calling convention;
see [Phonon return](../../phonon/reference/return.md).

See the [finite controller language](../../../../../language/controller.md)
for exact value and control-flow semantics.
