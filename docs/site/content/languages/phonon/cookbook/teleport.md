# Quantum teleportation

## What it does

Move a qubit's state from one qubit (`src`) to another (`dst`) using
an entangled pair (`src`, `anc`) and **two classical bits** sent
between them. The state never travels through the wire.

## Recipe

```phonon
target generic

def teleport(qubit src, qubit anc, qubit dst) {
    ; 1. Prepare a Bell pair between anc and dst.
    h anc
    cx anc, dst

    ; 2. Bell-measure src and anc.
    cx src, anc
    h src
    bit m_src[1]
    bit m_anc[1]
    m_src = measure src
    m_anc = measure anc

    ; 3. Apply classical-controlled corrections to dst.
    if (m_anc[0] == 1) { x dst }
    if (m_src[0] == 1) { z dst }
}

qubit q[3]
; ... prepare q[0] in the state to be teleported ...
teleport(q[0], q[1], q[2])
; q[2] now holds what q[0] used to hold.
```

## Why it works

Standard Bell-state measurement projects the joint state of `src` and
`anc` onto one of four Bell states; the result is two classical bits
identifying which one. Sending those bits to `dst` lets the receiver
apply the correct one of `{I, X, Z, XZ}` corrections — restoring
exactly what `src` was.

## Variations

- **Swap the X and Z** — they don't commute, so order matters.
- **Skip the corrections** if you only care about probabilities of
  `dst`. (Don't actually do this; it's wrong half the time.)
- **Reverse direction**: with extra entanglement, run the protocol
  backwards.

## Same in Photon

```photon
kernel teleport_demo() -> int {
    QReg q(3)
    q.teleport(0, 1, 2)
    return q.measure_int()
}
```

## Side effects on cost

3 qubits, ~6 single-qubit gates, 2 CX, 2 measurements, 2 conditional X/Z.
On `quantinuum_helios` (full feedforward): runs cleanly. On `ionq_forte`
(no feedforward): post-selection, effective shots ~25%.

## Where to look

- Reference: [`if`](../reference/if.md), [`def`](../reference/def.md), [`measure`](../../spinor/reference/measure.md)
- Photon library: [`teleport`](../../photon/reference/library/teleport.md)
- Source: [`photon/lang/cpp/lib/Library.cpp` `expandTeleport`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Library.cpp)
