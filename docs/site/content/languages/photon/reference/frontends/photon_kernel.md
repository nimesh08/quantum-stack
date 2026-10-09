# `@photon.kernel` — Python frontend

The decorator translates supported Python source into Phonon. Its optional C++
`compiled` handle is a **logical** program: `stage == "logical"` and
`target_verified == False`. Target-native compilation and execution use qstack.

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
```

`bell.phonon_text` exposes translated source. `bell.compiled.estimate()` reports
logical operation counts, including a depth proxy; it is not a physical schedule.
The explicit `photon.compile_logical` name and compatibility `compile_phonon`
name share that boundary.

`bell.run(shots=1000, target=DEVICE, provider=ROUTE, mode="local")` uses the real
C++ local simulator through qstack. `mode="live"` requires an authenticated,
compatible provider route. Numeric kernel parameters are bound before compilation.
Returned counts project only the declared return register or typed value.

The frontend also supports saved Boolean values, UInt1..64, branch joins,
`while photon.bounded(..., max_iterations=N)`, break/continue, conditional returns,
and scoped quantum allocation/discard. A loop exhaustion or unchecked loop
completion raises `PhotonKernelError`; `error.result` retains the full saved
execution result and all shots.

See the [finite controller language](../../../../../../language/controller.md)
and [unsupported constructs](../../rules/unsupported_constructs.md).
