# Repeated routine — `def` + `for` + `reset`

## What it does

Run the same subroutine many times, with reset in between, to
collect statistics or implement repetition codes.

## Recipe

```phonon
target ibm_heron_r2          ; needs supports.reset

def measure_in_x(qubit qq, bit out[1]) {
    h qq
    out = measure qq
    reset qq
}

qubit q[1]
bit  c[10]
for i in 0..10 {
    bit tmp[1]
    measure_in_x(q[0], tmp)
    c[i] = tmp[0]            ; (illustrative; bit-array assignment varies)
}
```

## Why it works

`def` defines the subroutine; `for` unrolls 10 calls; `reset`
brings the qubit back to `|0⟩` between iterations. The optimizer
sees a flat sequence of (h, measure, reset) × 10.

## Variations

- **Without reset** (chips that lack it): each iteration has to use
  a fresh qubit — `qubit q[10]` and call on `q[i]`.
- **Different parameters per call**: `for i in 0..10 { measure_in_x(q[i % 2], ...) }`
  alternates between two qubits.

## Same in Photon

Same idea; Photon's library has `q.measure()` returning a `bit[N]`,
and you can call methods in a `for` loop.

## Side effects on cost

10 × (1 H + 1 measure + 1 reset). Plus measurement readout overhead.
Effective `shots = 10 * shots_per_run`.

## Where to look

- Reference: [`def`](../reference/def.md), [`for`](../reference/for.md), [`reset`](../../spinor/reference/reset.md)
- Cookbook: [reset_and_reuse](../../spinor/cookbook/reset_and_reuse.md)
