# Photon — lexical structure

What [`photon/lang/cpp/lib/Lexer.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Lexer.cpp) recognises.

## Comments

Three forms accepted:

```photon
// C++ line comment
# Python-style line comment
; Spinor-style line comment
/* C-style block comment */
```

## Reserved keywords

| Keyword | Token | Used in |
|---|---|---|
| `target` | Target | `target generic` |
| `kernel` | Kernel | top-level kernel declaration |
| `def` | Def | helper functions |
| `return` | Return | kernel + def bodies |
| `for` | For | counted loops |
| `if` / `else` | If / Else | conditionals |
| `in` | In | `for i in 0..N` |
| `int` | Int | parameter / return type |
| `angle` | Angle | parameter type |
| `bit` / `Bit` | Bit | classical bit |
| `QReg` | QReg | quantum register |
| `pi` | Pi | π constant |

## Gate-method names (recognised by the parser)

```
h x y z s sdg t tdg
rx ry rz
cx cz swap
sx sxdg
ecr ms rzz
gpi gpi2 u1q

cnot hadamard phase           ; aliases
```

(See [`gateMethods()` in Lexer.cpp:62-74](https://github.com/nimesh08/quantum-stack/blob/main/photon/lang/cpp/lib/Lexer.cpp#L62-L74).)

## Operators and punctuation

| Token | Token kind |
|---|---|
| `(`, `)` | LParen / RParen |
| `{`, `}` | LBrace / RBrace |
| `[`, `]` | LBracket / RBracket |
| `.` | Dot |
| `..` | DotDot (range) |
| `,` | Comma |
| `:` | Colon |
| `;` | (only as comment start) |
| `+`, `-`, `*`, `/` | arithmetic |
| `->` | Arrow (return type) |
| `=`, `==`, `!=` | assign / equality |
| `<`, `<=`, `>`, `>=` | comparisons |
| `"..."` | StringLit |

## Literals

- **Integer**: `1`, `42`, `1000`.
- **Real**: `0.5`, `1.5e-2`.
- **String**: `"hello"`, with `\n`, `\t`, `\"`, `\\` escapes.
- **`pi`**: lexed as a token; `pi/2`, `2*pi`, `-pi/4` accepted.

## File extension

By convention `.pho`. The Phase C compiler drives `.pho` directly via
`photon::lang::parse(text, "filename.pho")`.

## See also

- [Grammar](grammar.md)
- [Types](types.md)
