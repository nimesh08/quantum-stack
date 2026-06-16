# Deutsch–Jozsa

## What it does

Tell whether an unknown function `f: {0,1}^n -> {0,1}` is
**constant** (always 0 or always 1) or **balanced** (0 on exactly
half the inputs) using **one** quantum query. Classically requires
2^(n-1) + 1 queries.

## Recipe (n=2)

```phonon
target generic

; Oracle for f(x) = x[0] XOR x[1]  (a balanced function)
def oracle(qubit q[3]) {
    cx q[0], q[2]
    cx q[1], q[2]
}

qubit q[3]
bit  c[2]
x q[2]                       ; ancilla starts in |1⟩
h q[0]
h q[1]
h q[2]
oracle(q)
h q[0]
h q[1]
c = measure q                ; reads q[0..1]; if 00, function is constant
```

## Why it works

The standard phase-kickback trick. After the H-oracle-H sandwich, the
amplitude on `|0...0⟩` is exactly `1` if `f` is constant and `0` if
balanced. So measuring `00...0` ↔ constant.

## Variations

- **Constant oracle**: `def oracle(qubit q[3]) { /* nothing */ }` —
  measurement returns `00` always.
- **n=3, n=4**: extend the qubit count.

## Same in Photon

You'd write a custom `Oracle` callback parameter to `q.grover` and
call it manually for D–J — the photon library doesn't ship a
dedicated D–J. But it's a few lines.

## Side effects on cost

n+1 qubits, 2n+1 hadamards, oracle-dependent CXes, n+1 measurements.
Trivially small. Good as a teaching demo, useless in production
(constant-vs-balanced is a toy problem).

## Where to look

- Reference: [`def`](../reference/def.md), [`if`](../reference/if.md)
- Cookbook: [grover](../../photon/cookbook/grover_with_oracle.md) for the
  oracle-based pattern in a real algorithm
