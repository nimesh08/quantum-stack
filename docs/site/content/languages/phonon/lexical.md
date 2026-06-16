# Phonon — lexical structure

Phonon is a strict superset of Spinor's lexer. Same tokens, plus a
handful of new keywords and operators for control flow and
expressions.

## Inherited from Spinor

Everything in [Spinor lexical](../spinor/lexical.md) — comments,
identifiers, gate mnemonics, numeric literals, the `pi` keyword,
`[`, `]`, `(`, `)`, `,`, `=`, `*`, `/`, `-`, newlines.

## New keywords

| Keyword | Token kind | Used in |
|---|---|---|
| `if` / `else` | If / Else | conditionals |
| `for` / `in` | For / In | counted loops |
| `while` | While | bounded loops |
| `def` | Def | function declarations |
| `return` | Return | function bodies |
| `int` | Int | parameter type |
| `angle` | Angle | parameter type |

(The Spinor keywords `target`, `generic`, `qubit`, `bit`, `measure`,
`reset`, `barrier`, `pi` are unchanged.)

## New operators

| Token | Meaning |
|---|---|
| `==` | equality |
| `!=` | inequality |
| `<`, `<=`, `>`, `>=` | comparisons |
| `..` | range (in `for`) |
| `{`, `}` | block delimiters |
| `+` | addition (in expressions) |

## Comments

Phonon accepts both Spinor's `;` and the C-style `//` for line
comments:

```phonon
; this is a comment
// this is also a comment
```

Block comments are not supported.

## Block syntax

Phonon uses C-style braces:

```phonon
def f(qubit q) {
    h q
    if (true) {
        x q
    }
}
```

Newlines inside a block are still significant — each statement still
ends with a newline, just like Spinor.

## File extension

By convention `.phn`. The compiler doesn't require it.

## See also

- [Grammar](grammar.md)
- [Spinor lexical](../spinor/lexical.md) (the inherited base)
