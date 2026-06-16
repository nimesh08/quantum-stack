# Phonon in 10 minutes

Phonon is **Spinor + classical structure + linear types**. Every Spinor
program is a legal Phonon program; you only reach for Phonon when you
need conditionals, loops, or functions.

## 1. The shape

```phonon
target generic

def bell_pair(qubit a, qubit b) {
    h a
    cx a, b
}

qubit q[2]
bit  c[2]
bell_pair(q[0], q[1])
c = measure q
```

`def` defines a function (always inlined). Braces delimit blocks. The
rest of the syntax is unchanged from [Spinor](../languages/spinor/index.md).

## 2. Conditionals

```phonon
qubit q[2]
bit  m[1]
h q[0]
m = measure q[0]
if (m[0] == 1) {
    x q[1]                   ; classically-controlled X
}
```

Three flavours of `if` ([reference](../languages/phonon/reference/if.md)):

- **Compile-time**: condition is `int` literals only — dead-code-eliminated.
- **Loop-index**: per-iteration unroll.
- **Feedforward**: condition reads a `bit[k]` from `measure` —
  needs chip support, falls back to post-selection.

## 3. Loops

```phonon
qubit q[3]
h q[0]
for i in 0..2 {              ; i = 0, 1
    cx q[i], q[i+1]
}
```

Bounds are integer literals. The compiler unrolls.

## 4. Functions

```phonon
def grover_step(qubit qq, angle theta) {
    rz(theta) qq
    h qq
}

qubit q[1]
for i in 0..3 {
    grover_step(q[0], pi/2)
}
```

Inlined at compile time. Parameters: `qubit`, `bit`, `int`, `angle`.

## 5. Linear types — the headline feature

The compiler **enforces no-cloning**:

```phonon
qubit q[2]
qubit r[1]
r[0] = q[0]                  ; ERROR: cannot duplicate a qubit
```

[`Linear types`](../languages/phonon/linear_types.md) walks through every
case the checker rejects.

## 6. Optimization happens here

Phonon is the IR the optimizer works on. Cancellation, rotation merging,
ZX simplification, scheduling — all of them happen on Phonon **before**
the chip-specific Spinor passes. Improvements in the Phonon optimizer
benefit every chip automatically.

## 7. Compile pipeline

```
.phn → tokens → AST → Phonon IR → linear-type check
     → optimize once → lower to Spinor → place/route/decompose → emit
```

## 8. Worked example: teleportation

```phonon
target generic

def teleport(qubit src, qubit anc, qubit dst) {
    h anc
    cx anc, dst
    cx src, anc
    h src
    bit m_src[1]
    bit m_anc[1]
    m_src = measure src
    m_anc = measure anc
    if (m_anc[0] == 1) { x dst }
    if (m_src[0] == 1) { z dst }
}

qubit q[3]
; ... prepare q[0] in some state ...
teleport(q[0], q[1], q[2])
```

`teleport` is **impossible to write in Spinor** — it needs the
classical-controlled X and Z. Phonon handles it.

## What's next

- The full [Phonon reference](../languages/phonon/index.md).
- [Linear types](../languages/phonon/linear_types.md) — the
  most important page.
- [Photon in 10 minutes](photon_walkthrough.md) — the OO layer
  on top of this.
