# Phonon — overview

A `.phn` source file is a Spinor file with **classical structure**
added: variables, conditionals, loops, functions. Every legal Spinor
program is also a legal Phonon program — Phonon is a strict superset.

## Why Phonon exists

Two reasons:

1. **Express programs that need classical structure.** Teleportation
   has to branch on a measurement result; Grover has to repeat the
   oracle-then-diffuse loop a fixed number of times. Spinor cannot do
   either.
2. **Be the IR the optimizer works on.** Spinor's gate sequence is
   too low-level for sophisticated rewrites; Photon's OO methods are
   too high-level. Phonon is the "right" level for cancellation,
   rotation merging, ZX simplification, scheduling.

## What Phonon adds

| Feature | Spinor | Phonon |
|---|---|---|
| `target` declaration | yes | yes |
| `qubit` / `bit` registers | yes | yes |
| Gates (h, cx, …) | yes | yes |
| `measure` / `reset` / `barrier` | yes | yes |
| `if` / `else` | — | yes |
| `for i in lo..hi { … }` | — | yes (constant bounds) |
| `while (cond) { … }` | — | yes (constant bound) |
| `def f(...) { … }` | — | yes |
| `return` | — | yes |
| Variables (`int`, `angle`, `bit`) | — | yes |
| Linear-type checking | (W3 only) | full no-cloning enforcement |

## A complete program

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

`def` defines a function; the call inlines at compile time. The
optimizer then sees a flat program identical to the hand-written
Bell.

## Linearity (the headline feature)

Phonon's type checker does one thing no classical type checker had to:
**enforce a law of nature**. Qubits cannot be copied. The compiler
catches that at your desk:

```phonon
qubit q[2]
qubit r[1]
r[0] = q[0]                  ; ERROR: cannot duplicate a qubit value
```

See [Linear types](linear_types.md) for the full story.

## What Phonon does not have

- **Recursion** — `def`s cannot call themselves directly or
  indirectly. Use `for` for repetition.
- **Dynamic allocation** — every register's size is known at parse
  time, just like in Spinor.
- **General expressions** in gate parameters — same four shapes as
  Spinor (`pi`, `pi/N`, `real`, `real*pi`). Phonon adds **identifier
  references** for parameters declared in a `def`'s signature.

## Compile pipeline

```
.phn source
  → tokens (lexer)
  → AST (parser)
  → Phonon dialect IR
  → linear-type check  ← rejects cloning here
  → optimize once (cancellation / merge / ZX / schedule)
  → lower to Spinor IR (target generic)
  → Spinor pipeline (placement / routing / decomp)
  → emit
```

The optimizer runs **once, above all chips**. Improvements there benefit
every supported chip automatically.

## Where to next

- [Lexical](lexical.md), [Grammar](grammar.md), [Types](types.md)
- [Linear types](linear_types.md) — most important page
- [Teleport cookbook](cookbook/teleport.md) — your first non-trivial Phonon program
