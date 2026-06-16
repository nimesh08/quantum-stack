# Parameterised circuit — `angle` parameter to a `def`

## What it does

Build a circuit where the rotation angles are *parameters*, not
literals. Useful for VQE, QAOA, and any variational algorithm.

## Recipe

```phonon
target generic

def ry_layer(qubit qq, angle theta) {
    ry(theta) qq
}

def entangling_layer(qubit q[3]) {
    cx q[0], q[1]
    cx q[1], q[2]
}

def vqe_step(qubit q[3], angle a, angle b, angle c) {
    ry_layer(q[0], a)
    ry_layer(q[1], b)
    ry_layer(q[2], c)
    entangling_layer(q)
}

qubit q[3]
vqe_step(q, pi/4, pi/3, pi/8)
```

## Why it works

`def` parameters of type `angle` are passed through to the underlying
`ry` calls. After inlining the `def`s, the optimizer sees a flat
program with concrete angles.

## Variations

- **Outer Python driver**: in practice you compile the .phn once with
  symbolic placeholders and pass the angles via a higher-level
  driver. The Photon Python frontend does this automatically.
- **Multi-layer ansatz**: nest `for i in 0..depth { vqe_step(q, ...) }`.

## Same in Photon

```photon
kernel vqe(angle a, angle b, angle c) -> int {
    QReg q(3)
    q.ry(a, 0)
    q.ry(b, 1)
    q.ry(c, 2)
    q.cx(0, 1)
    q.cx(1, 2)
    return q.measure_int()
}
```

## Side effects on cost

Same as if you wrote the angles literally — the compiler folds them
in at lowering. The optimizer can simplify rotation chains
(`ry(a) ry(b) → ry(a+b)`) once concrete.

## Where to look

- Reference: [`def`](../reference/def.md), Spinor [`ry`](../../spinor/reference/ry.md)
- Photon library: [`vqe_ansatz`](../../photon/reference/library/vqe_ansatz.md)
