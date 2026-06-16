# `q.bell_pair(a, b)` — Bell pair  *[photon.lib]*

Build the Bell state $(|00\rangle + |11\rangle)/\sqrt{2}$ between qubits `a` and `b`.

## Synopsis

```photon
q.bell_pair(a, b)            ; defaults to a=0, b=1 if no args
```

## Parameters

| name | type | default |
|---|---|---|
| a | `int` | 0 |
| b | `int` | 1 |

Both must be **compile-time foldable** to ints — the C++ implementation
calls `ctx.foldInt(args[0])` and rejects with
"bell_pair: indices must fold at compile time" otherwise.

## What it expands to

```photon
q.h(a)
q.cx(a, b)
```

Source:
[`photon/lang/cpp/lib/Library.cpp` `expandBellPair`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L83-L102).

## Examples

```photon
kernel bell() -> int {
    QReg q(2)
    q.bell_pair()            ; (a=0, b=1) defaults
    return q.measure_int()
}
```

```photon
kernel bell_2_3() -> int {
    QReg q(4)
    q.bell_pair(2, 3)
    return q.measure_int()
}
```

## See also

[`ghz`](ghz.md), [Spinor cookbook: bell](../../../spinor/cookbook/bell.md)
