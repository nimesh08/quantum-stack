# Reset and reuse a qubit

## What it does

Frees up a qubit after measurement so you can use it again — useful
when your program needs more qubits than the chip has (rare for
demos, common in production).

## Recipe

```spinor
target ibm_heron_r2          ; chip with supports.reset: true
qubit q[1]
bit  c[2]
h q[0]
c[0] = measure q[0]          ; q[0] now in a basis state
reset q[0]                   ; q[0] back to |0⟩
h q[0]
c[1] = measure q[0]          ; second independent measurement on q[0]
```

## Why it works

`reset` is a hardware primitive on most modern chips. After the
measurement, the chip applies an X iff the result was 1, putting
the qubit back to `|0⟩`. The compiler enforces W4: no gate on a
measured qubit until you `reset`.

## Variations

- **Without `reset`**: ERROR W4 (try it).
- **On a chip without `supports.reset`**: the legalisation pass
  refuses with a precise error pointing at the chip YAML.
- **In Photon**: `q.reset(0)`.

## Same in Phonon

Identical — Phonon adds the option to condition the reset on a measured
bit:

```phonon
if (c[0] == 1) { x q[0] }     ; manual reset via feedforward
```

(only on chips with `supports.feedforward`).

## Same in Photon

```photon
QReg q(1)
q.h(0)
mid_bit = q.measure(0)
q.reset(0)
q.h(0)
```

## Side effects on cost

A `reset` is typically the cost of one measurement + a conditional
X. ~equal to one two-qubit gate of error.

## Where to look

- Reference: [`measure`](../reference/measure.md), [`reset`](../reference/reset.md)
- Rule: [W4](../rules/W4_no_op_after_measure.md)
