# Spinor — lexical structure

What the [lexer](https://github.com/nimesh08/quantum-stack/blob/main/spinor/parser/cpp/lib/Lexer.cpp) recognises.

## Whitespace

Spaces, tabs, and carriage returns are insignificant *within* a line.
**Newlines are significant** — they terminate statements (mostly).
Runs of newlines + comments collapse to a single `Newline` token, so
blank lines between statements are fine.

## Comments

```
; everything from a semicolon to end-of-line is a comment
target generic   ; trailing comments are fine too
```

There are no block comments in Spinor.

## Identifiers

```
identifier ::= letter { letter | digit | "_" }
```

Letter-or-underscore start, then any of `[A-Za-z0-9_]`. Used for
register names (`q`, `c`, `theta_reg`).

## Reserved keywords

| Keyword | Token kind | Meaning |
|---|---|---|
| `target` | `Target` | start of file |
| `generic` | `Generic` | the portable target id |
| `qubit` | `Qubit` | declare a qubit register |
| `bit` | `Bit` | declare a classical bit register |
| `measure` | `Measure` | measurement statement |
| `reset` | `Reset` | reset to \|0⟩ |
| `barrier` | `Barrier` | optimizer fence |
| `pi` | `Pi` | the constant π in gate parameters |

## Gate mnemonics (lexed as `GateName`)

The full set the lexer recognises (see
[Lexer.cpp:13–19](https://github.com/nimesh08/quantum-stack/blob/main/spinor/parser/cpp/lib/Lexer.cpp#L13-L19)):

```
h    x    y    z    s    sdg    t    tdg
rx   ry   rz
cx   cz   swap
ecr  ms   rzz   sx   sxdg
gpi  gpi2 u1q
```

Anything that's not in this set + isn't a reserved keyword is lexed as
an [`Identifier`](#identifiers) (used as a register name).

## Numeric literals

```
integer ::= digit { digit }
real    ::= [ "-" ] digit { digit } [ "." digit { digit } ]
```

The lexer also accepts the `eE` exponent form for reals
(`1.5e-2`). Integers and reals are distinct token kinds.

A unary minus before a numeric literal is **lexed as the `Minus`
operator**, not folded into the literal — so `rx(-pi)` parses as
`rx(<minus> pi)`, not `rx(<negative-pi>)`. The parser handles the
folding.

## Punctuation

| Token | Lexer kind |
|---|---|
| `[` | `LBracket` |
| `]` | `RBracket` |
| `(` | `LParen` |
| `)` | `RParen` |
| `,` | `Comma` |
| `=` | `Equals` |
| `*` | `Star` |
| `/` | `Slash` |
| `-` | `Minus` |

## Newline + EOF

`Newline` is its own token kind; `Eof` ends the stream. The parser
[`spinor::parser::Driver`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/parser/cpp/lib/Parser.cpp)
uses `expectNewlineOrEof()` to terminate statements.

## What's not in the lexer

- No string literals.
- No keyword `true` / `false`.
- No `#` directive lines.
- No backslash line-continuation.

If you need any of those, you want [Phonon](../phonon/index.md).
