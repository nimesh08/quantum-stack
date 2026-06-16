# `q.teleport(src, anc, dst)` — quantum teleportation  *[photon.lib]*

Move `q[src]`'s state to `q[dst]` using `q[anc]` as ancilla and two
classical bits.

## Synopsis

```photon
q.teleport(src, anc, dst)
```

## Parameters

| name | type | default |
|---|---|---|
| src | `int` | 0 |
| anc | `int` | 1 |
| dst | `int` | 2 |

All three must be compile-time foldable.

## What it expands to

The full teleportation protocol — Bell pair on (anc, dst), Bell-measure
on (src, anc), classical-controlled X and Z on dst. Source:
[`photon/lang/cpp/lib/Library.cpp` `expandTeleport`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp#L206-L239).

## Examples

```photon
kernel teleport_demo() -> int {
    QReg q(3)
    // ... prepare q[0] in some interesting state ...
    q.teleport(0, 1, 2)
    return q.measure_int()    // q[2] now holds what q[0] used to
}
```

## See also

[Cookbook: teleport_full](../../cookbook/teleport_full.md), [Phonon teleport](../../../phonon/cookbook/teleport.md)
