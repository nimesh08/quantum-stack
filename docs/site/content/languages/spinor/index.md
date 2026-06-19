# Spinor

Spinor is the **lowest layer a human still writes** — quantum
assembly. One line per machine operation, no loops, no functions, no
expressions. Twenty-two grammar rules, fourteen core gates plus eight
native ones, and that is the entire language.

If Phonon and Photon feel "too magical", drop down to Spinor: every
gate you write becomes one gate the chip executes (modulo placement
and routing).

## A complete program

```spinor
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

That is the [Bell state](tutorial.md) — three statements declare,
three statements compute. Save as `bell.spn` and run:

```bash
spinorc compile -t ibm_heron_r2 bell.spn
```

You get back the same program rewritten for IBM Heron r2's heavy-hex
topology, using only ECR + RZ + SX + measure. Same Spinor language;
two contracts.

## The two contracts

Every Spinor file lives under exactly one of two contracts, declared
by its first line.

| Contract | First line | Gate set | Qubits | Audience |
|----------|-----------|----------|--------|----------|
| **Portable** | `target generic` | Standard set: `h x y z s sdg t tdg rx ry rz cx cz swap`. | Virtual labels (think variables). All-to-all coupling. | What humans write. |
| **Hardware** | `target <chip-id>` | Only the chip's `native_gates`. | Physical positions on the silicon; `coupling_map` enforced. | What `spinorc compile` produces. |

The compile pipeline takes a portable `.spn` and lowers it through
**placement → routing → decomposition → cleanup → emit** into a wire
format the provider accepts (OpenQASM 3, QIR, or Quil).

## What is in this section

- [Install](install.md) — get the `spinorc` binary on your machine.
- [Tutorial](tutorial.md) — the Bell program, line by line, end to
  end through every layer.
- [Lexical structure](lexical.md) — every token the lexer produces.
- [Grammar](grammar.md) — the 22 EBNF rules, walked through.
- [Types](types.md) — `qubit q[N]` versus `bit c[N]`.
- [Cookbook](cookbook/index.md) — short, runnable recipes (Bell,
  GHZ, parameterised rotations, native gate sets, routing via SWAP).

## What Spinor does **not** have

- No variables, functions, conditionals, or loops. Those live in
  [Phonon](../phonon/index.md).
- No strings, no floating-point arithmetic beyond gate parameters.
- Nothing dynamic. Spinor is fully static; every `qubit` and `bit`
  array's size is known at parse time.

## Where to put new code

| Adding ... | Folder |
|---|---|
| A new gate mnemonic | [`spinor/parser/cpp/lib/Lexer.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/parser/cpp/lib/Lexer.cpp) (the gate set) plus a passes change |
| A new chip | [`spinor/registry/chips/<id>.yaml`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips) — **no compiler change** |
| A new emitter | [`spinor/emit/`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/emit) |

---

!!! quote "Credits"
    **Spinor** was designed and implemented by **Nimesh Cheedella**
    as the chip-locking layer of the Heisenberg quantum stack.
