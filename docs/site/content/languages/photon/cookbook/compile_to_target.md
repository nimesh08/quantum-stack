# Compile to multiple targets

## What it does

The same Photon kernel compiles for any chip — pick at submission time.

## Recipe

```photon
target generic               ; portable; compiler picks the chip later

kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}
```

Compile for three chips:

```python
import photon
prog = photon.compile_phonon(bell.phonon_text, target="generic")
prog_ibm   = photon.compile_phonon(bell.phonon_text, target="ibm_heron_r2")
prog_ionq  = photon.compile_phonon(bell.phonon_text, target="ionq_forte")

print(prog_ibm.estimate())
print(prog_ionq.estimate())
```

## Resource differences

| Chip | num_qubits | two_qubit_count | depth | $/1000 shots |
|---|---|---|---|---|
| `generic` | 2 | 1 | 4 | $0.00 |
| `ibm_heron_r2` | 2 | 1 ECR + ~4 single | ~7 | $0.330 |
| `ionq_forte` | 2 | 1 MS + ~3 single | ~6 | $10.000 |

## When to recompile

The Phonon optimizer runs once on the portable program; the
chip-specific lowering runs per-target. Recompiling for a new chip
**does not** rerun the optimizer — it's cheap.

## See also

[Cookbook: chip_specialization](../../phonon/cookbook/chip_specialization.md), [`@photon.kernel`](../reference/frontends/photon_kernel.md)
