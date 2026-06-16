# `q.vqe_ansatz(depth)` — VQE-style variational ansatz  *[photon.lib]*

A simple layered ansatz: per layer, an Ry rotation on every qubit, then a chain of CXes.
Used as a starting point for VQE / QAOA experiments.

## Synopsis

```photon
q.vqe_ansatz(depth)
```

## Parameters

| name | type | constraint |
|---|---|---|
| depth | `int` | compile-time foldable; ≥ 1 |

## What it expands to

```
for d in 0..depth {
    for i in 0..N {
        q.ry(0.1 * (d*N + i), i)    // baked-in increasing angles
    }
    for i in 0..(N-1) {
        q.cx(i, i+1)
    }
}
```

The angles are **fixed** in this M2-era implementation — variational
loops run an outer Python driver that recompiles per parameter set.
A future version will accept an `angle[]` parameter. Source:
[`photon/lang/cpp/lib/Library.cpp` `expandVqeAnsatz`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L242-L265).

## Examples

```photon
kernel vqe_4q(int depth) -> int {
    QReg q(4)
    q.vqe_ansatz(depth)
    return q.measure_int()
}
```

## See also

[Cookbook: variational_loop](../../cookbook/variational_loop.md)
