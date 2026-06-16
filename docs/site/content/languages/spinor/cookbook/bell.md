# Bell program

## What it does

Prepares the entangled state $(|00\rangle + |11\rangle)/\sqrt{2}$ — the
simplest quantum program that does anything non-classical. Two qubits
that always agree on every measurement.

## Recipe

```spinor
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

## Why it works

`h q[0]` turns `|0⟩` into `(|0⟩+|1⟩)/√2`. `cx q[0], q[1]` flips q[1]
in the `|1⟩` branch only, producing $(|00\rangle + |11\rangle)/\sqrt{2}$. Measurement
yields `00` or `11` with equal probability; `01` and `10` never appear.

## Variations

- **3-qubit GHZ**: see [`ghz.md`](ghz.md).
- **Negative Bell**: insert `z q[0]` before the H to get $(|00\rangle - |11\rangle)/\sqrt{2}$.
- **Mid-circuit measure** (chips with `supports.mid_circuit_measure`):
  measure `q[0]` mid-program, condition `q[1]` based on the outcome
  in [Phonon](../../phonon/cookbook/teleport.md).

## Same in Phonon

Identical — Bell uses no control flow:

```phonon
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

## Same in Photon (.pho)

```photon
kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}
```

## Same in `@photon.kernel` (Python)

```python
import photon

@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
```

## Side effects on cost

| Resource | Value |
|---|---|
| `num_qubits` | 2 |
| `two_qubit_count` | 1 |
| `depth` | 4 |
| `t_count` | 0 |
| Cost on `ibm_heron_r2` @ 1000 shots | $0.330 |
| Cost on `ionq_forte` @ 1000 shots | $10.000 |

## Where to look

- Reference: [`h`](../reference/h.md), [`cx`](../reference/cx.md), [`measure`](../reference/measure.md)
- Tutorial: [Bell, end to end](../../../tutorial/bell.md)
- Test corpus: [`spinor/tests/corpus/bell.spn`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/tests/corpus/bell.spn)
