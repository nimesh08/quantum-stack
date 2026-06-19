# Spinor — overview

A `.spn` source file is one [`target`](reference/target.md) line followed
by a sequence of statements: register declarations, gate applications,
measurements, resets, and barriers. Twenty-two grammar rules, fourteen
core gates plus eight native ones — and that's the entire language.

## The two contracts

Every Spinor file lives under exactly **one** of two contracts,
declared by its first line:

### Portable contract — `target generic`

- Standard gate vocabulary only: `h, x, y, z, s, sdg, t, tdg, rx, ry,
  rz, cx, cz, swap`.
- Any qubit may interact with any qubit (all-to-all).
- Qubit numbers are **virtual** labels (think variables).

This is what humans write.

### Hardware contract — `target <chip>`

- Only the chip's [`native_gates`](https://github.com/nimesh08/quantum-stack/tree/main/spinor/registry/chips) are legal.
- Qubit numbers are **physical** positions on the silicon.
- Every two-qubit instruction must sit on a wired pair of `chip.coupling_map`.

This is what the compiler **produces**.

The compile pipeline takes a portable `.spn` and lowers it through
placement → routing → decomposition → cleanup → an emitter into
provider format. The same Spinor language describes both ends.

## A complete program

```spinor
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

That's the [Bell state](cookbook/bell.md) — the simplest entangled
program. Three statements declare, three statements compute.

## Comments

Use `;` to start a line comment.

```spinor
; this is a comment
target generic     ; trailing comments are also fine
qubit q[2]
```

## File extension

By convention `.spn`. The Spinor compiler doesn't care about the
extension; it sniffs the first non-comment line for `target`.

## What Spinor does **not** have

- Variables, functions, conditionals, loops — those live in
  [Phonon](../phonon/index.md).
- Strings, floating-point arithmetic beyond gate parameters.
- Anything dynamic. Spinor is fully static; every `qubit` and `bit`
  array's size is known at parse time.

## Where to next

- [Lexical structure](lexical.md) — every token type
- [Grammar](grammar.md) — the 22 production rules
- [Types](types.md) — `qubit` vs `bit`, indexing, arrays
- [Bell cookbook](cookbook/bell.md) — your first program, end to end
