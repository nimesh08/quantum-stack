# Teleport, end to end

## What it does

Teleport an arbitrary state from `q[0]` to `q[2]` using `q[1]` as ancilla.

## Recipe

```photon
target quantinuum_helios     // chip with full feedforward

kernel teleport_demo() -> int {
    QReg q(3)
    // 1. Prepare q[0] in some interesting state.
    q.h(0)
    q.t(0)                   // arbitrary phase

    // 2. Teleport q[0]'s state to q[2].
    q.teleport(0, 1, 2)

    // 3. Measure q[2] — should reflect the original q[0].
    return q.measure_int()
}
```

## Why it works

See the [Phonon teleport recipe](../../phonon/cookbook/teleport.md) for
the math. Photon's `q.teleport` library call expands to the same
gate sequence.

## Variations

- **Run on a feedforward-less chip**: post-selection halves your
  effective shot count. Set `shots = 2 * desired_shots` to compensate.
- **Multiple teleports**: chain `q.teleport(0,1,2); q.teleport(2,3,4)`.

## See also

Library: [`teleport`](../reference/library/teleport.md), [Phonon teleport](../../phonon/cookbook/teleport.md)
