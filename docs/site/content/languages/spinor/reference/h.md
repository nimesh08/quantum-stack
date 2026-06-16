# `h` — Hadamard gate  *[one-qubit Clifford]*

The "blender". Maps `|0⟩` to `(|0⟩+|1⟩)/√2` (an even superposition)
and `|1⟩` to `(|0⟩-|1⟩)/√2`. Self-inverse: `h h = I`.

## Synopsis

```
h <qubit>
```

## Parameters

| name | type | constraint |
|---|---|---|
| qubit | qubit operand | declared (W1), in range (W2) |

## Matrix

$$\frac{1}{\sqrt{2}}\begin{pmatrix} 1 & 1 \\ 1 & -1 \end{pmatrix}$$

## Semantics

$|0\rangle \mapsto \tfrac{1}{\sqrt{2}}(|0\rangle + |1\rangle)$
$|1\rangle \mapsto \tfrac{1}{\sqrt{2}}(|0\rangle - |1\rangle)$

Equivalent to a continuous rotation: $H = R_x(\pi/2) R_z(\pi/2) R_x(\pi/2)$
up to a global phase.

## Legality

- W1, W2 — operand must be declared and in range.
- W4 — illegal on a measured qubit unless the chip has
  `mid_circuit_measure: true` and a `reset` was inserted.
- W5 / W6 — legal under both contracts.

## Examples

```spinor
target generic
qubit q[1]
h q[0]                       ; q[0] now in (|0⟩+|1⟩)/√2

h q[0]                       ; second h cancels: H·H = I
```

```spinor
qubit q[2]
h q[0]
cx q[0], q[1]                ; build a Bell pair
```

## Lowering

| Chip native gate set | `h` decomposes to |
|---|---|
| IBM Heron r2 (`ecr rz sx x`) | `rz(π/2) sx rz(π/2)` (up to global phase) |
| IonQ Forte (`ms rzz gpi gpi2`) | `gpi2(π/2)` |
| Quantinuum Helios (`u1q ms rzz`) | `u1q` with the matching parameter |

## Equivalents

- **Phonon**: identical — `h q[0]`.
- **Photon**: `q.h(0)` or alias `q.hadamard(0)`.
- **Python (`@photon.kernel`)**: `q.h(0)`.

## See also

- [`x`](x.md), [`z`](z.md) — the other two single-qubit Paulis
- [`rx`](rx.md), [`ry`](ry.md), [`rz`](rz.md) — what `h` decomposes into
- [Cookbook: Bell program](../cookbook/bell.md)
