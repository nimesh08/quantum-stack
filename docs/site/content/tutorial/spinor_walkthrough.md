# Spinor in 10 minutes

A guided tour of the assembly-level language. By the end you'll be
able to read, write, and compile a Spinor program. No prior quantum
or compiler background assumed.

## 1. The shape of every Spinor program

Every `.spn` file starts with one [`target`](../languages/spinor/reference/target.md)
line, then declarations, then gates, then measurements:

```spinor
target generic
qubit q[2]      ; declare a 2-qubit register
bit c[2]        ; declare a 2-bit classical register
h q[0]          ; gate
cx q[0], q[1]   ; gate
c = measure q   ; measurement
```

`;` starts a comment. Newlines terminate statements. That's the
shape — three sections.

## 2. The two contracts

The first line picks one of two **contracts**:

- `target generic` — write portable assembly. Any standard gate, any
  pair of qubits.
- `target ibm_heron_r2` (or any chip id) — write **chip-locked**
  assembly. Only that chip's native gates, only on physically wired
  pairs.

Humans write the first form. The compiler **produces** the second.

```spinor
; portable
target generic
qubit q[2]
h q[0]
cx q[0], q[1]
```

```spinor
; the same program after compiling for IBM Heron r2:
target ibm_heron_r2
qubit q[2]            ; placement chose physical qubits 17, 18
sx q[17]              ; h rebuilt from sx + rz
rz(1.5708) q[17]
ecr q[17], q[18]      ; cx rebuilt around the native ECR
sx q[18]
rz(1.5708) q[18]
```

## 3. The gates

You have 22 mnemonics ([full list](../languages/spinor/index.md#reference-every-keyword-has-its-own-page)):

- One-qubit Cliffords: [`h`](../languages/spinor/reference/h.md), `x`, `y`, `z`, `s`, `sdg`.
- Non-Clifford: `t`, `tdg`.
- Continuous: [`rx`](../languages/spinor/reference/rx.md), `ry`, `rz`.
- Two-qubit: [`cx`](../languages/spinor/reference/cx.md), `cz`, `swap`.
- Native (hardware contract): `ecr`, `ms`, `rzz`, `sx`, `sxdg`, `gpi`, `gpi2`, `u1q`.

Each gate has its own page with the matrix, semantics, examples, and
how it lowers to each chip.

## 4. Measurement and reset

```spinor
c = measure q       ; whole-array measurement
c[0] = measure q[0] ; single-qubit
reset q[0]          ; back to |0⟩
```

[`measure`](../languages/spinor/reference/measure.md) collapses the qubit
into the bit. [`reset`](../languages/spinor/reference/reset.md) lets you
re-use a qubit (only on chips with `supports.reset: true`).

## 5. Parameters

The continuous-rotation gates take an angle:

```spinor
rz(pi)       q[0]      ; π
rz(pi/2)     q[0]      ; quarter turn
rz(0.7853)   q[0]      ; raw radians
rz(-pi/4)    q[0]      ; unary minus
```

Four shapes — that's the whole language for parameters. No general
expressions.

## 6. Compile it

After [installing `spinorc`](../install/spinorc_cli.md):

```bash
echo 'target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q' > bell.spn

spinorc compile -t ibm_heron_r2 bell.spn   # placement + routing + decomp
spinorc check   -t ibm_heron_r2 bell.spn   # equivalence + resource estimate
spinorc emit    -t ibm_heron_r2 -f qasm3 --verbatim bell.spn
```

## 7. The legality rules

Six small rules, enforced by the verifier:

| | Rule |
|---|---|
| W1 | declare before use |
| W2 | indices in range |
| W3 | distinct operands on a multi-qubit gate (no-cloning, syntactically) |
| W4 | no gate on a measured qubit (chips without `mid_circuit_measure`) |
| W5 | portable contract — only standard gates |
| W6 | hardware contract — native gates + connected pairs |

See [Legality rules](../languages/spinor/index.md#legality-rules) for
each with worked errors.

## 8. What's next

- The [Bell cookbook](../languages/spinor/cookbook/bell.md) walks you
  through the smallest entangler, end to end.
- [Add a chip in 30 minutes](add_a_chip.md) demonstrates the
  YAML-only-no-compiler-change claim.
- For control flow (`if`, `for`, `def`), look at
  [Phonon in 10 minutes](phonon_walkthrough.md).
