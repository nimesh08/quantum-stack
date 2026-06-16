# `q.grover(oracle, rounds)` — Grover search  *[photon.lib]*

Quantum search. Given an `Oracle` callback that marks a "needle"
state, find that needle in O(√N) calls.

## Synopsis

```photon
q.grover(<oracle>, <rounds>)
```

## Parameters

| name | type | constraint |
|---|---|---|
| oracle | `Oracle` | parameter declared in the kernel signature |
| rounds | `int` | compile-time foldable; ≥ 1 |

## What it expands to

```
H^N
for r in 0..rounds {
    oracle()                 // user-supplied phase-flip
    diffusion()              // H^N · X^N · multi-CZ · X^N · H^N
}
```

Source:
[`photon/lang/cpp/lib/Library.cpp` `expandGrover`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L181-L203).

## The `Oracle` parameter

`Oracle` is a parameter type — the kernel receives it from the caller.
Inside the kernel, we just call `oracle(...)` and the C++ engine
inlines it.

```photon
def my_oracle(QReg q) {
    q.cx(0, q.size() - 1)    // mark some state
}

kernel search(Oracle f) -> int {
    QReg q(4)
    q.grover(f, 3)           // 3 rounds is optimal for ~1 in 16
    return q.measure_int()
}
```

## Optimal round count

For a single needle in N=2^n basis states, the optimal round count is
≈ ⌊π/4 · √N⌋. For 4 qubits (16 basis states), use 3 rounds.

## See also

[`Oracle` type](../types.md#oracle), [Cookbook: grover_with_oracle](../../cookbook/grover_with_oracle.md)
