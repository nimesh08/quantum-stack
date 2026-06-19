# Spinor — cookbook

Bite-sized, runnable recipes. Pick one, copy-paste, run. Every
recipe assumes you have `spinorc` on your `PATH`
([install](../install.md)).

## Programs

- [Bell pair](bell.md) — the smallest entangler; the canonical first
  program.
- [GHZ state](ghz.md) — n-qubit entanglement; same idea as Bell,
  scaled.

## Working with the gate set

- [Arbitrary rotation](arbitrary_rotation.md) — `rx`, `ry`, `rz` and
  how they compose.
- [Parameterised gates](parameterized_gates.md) — `pi`, `pi/2`,
  `2*pi/3`, and what the parser accepts.
- [`barrier` for the optimizer](barrier_for_optimizer.md) — when to
  forbid reordering across a fence.
- [Reset and reuse](reset_and_reuse.md) — `measure → reset` lets you
  reuse a qubit (chips with `mid_circuit_measure`).

## Native gate sets

- [IBM native gate set](ibm_native_gates.md) — `ECR + RZ + SX`.
- [IonQ native gate set](ionq_native_gates.md) — `MS + RZZ + GPI / GPI2`.

## Routing and topology

- [Routing via `swap`](routing_via_swap.md) — what the SABRE pass
  does when two qubits are not adjacent.
- [All-to-all vs heavy-hex](all_to_all_vs_heavy_hex.md) — the same
  program compiled to two different topologies.

---

!!! quote "Credits"
    **Spinor** was designed and implemented by **Nimesh Cheedella**.
