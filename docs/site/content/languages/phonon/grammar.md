# Phonon — grammar

Phonon's grammar is **Spinor's grammar plus the rules below**. Every
Spinor program is a legal Phonon program; the converse is not true.

## The delta over Spinor

```ebnf
statement ::= ( any Spinor statement )
            | if_stmt | for_stmt | while_stmt
            | func_decl | call_stmt | assign_stmt ;

block ::= "{" { statement } "}" ;

if_stmt ::= "if" "(" condition ")" block [ "else" block ] ;

for_stmt ::= "for" identifier "in" integer ".." integer block ;

while_stmt ::= "while" "(" condition ")" block ;

func_decl ::= "def" identifier "(" [ param { "," param } ] ")" block ;
param     ::= type identifier ;
type      ::= "qubit" | "bit" | "int" | "angle" ;

call_stmt ::= identifier "(" [ expression { "," expression } ] ")" NEWLINE ;

assign_stmt ::= identifier "=" expression NEWLINE ;

condition ::= expression ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) expression ;

expression ::= integer | real | identifier | identifier "[" expression "]"
             | expression "+" expression | expression "*" expression
             | "pi" | "pi" "/" integer | "(" expression ")" ;
```

## Walkthrough

### Conditionals

```phonon
if (m_anc[0] == 1) {
    x dst
}
```

Parens around the condition; braces around the body. `else` is
optional.

### Counted loops

```phonon
for i in 0..n {
    cx q[i], q[i+1]
}
```

The bounds **must be integer literals**; the lower bound is
inclusive, the upper bound is **exclusive**. `i` is bound only inside
the block.

### `while` loops

```phonon
while (i < 4) {
    h q[i]
    i = i + 1                ; assign + integer arithmetic
}
```

`while` requires the loop terminate at compile time
(see [compile-time bounds](rules/compile_time_bounds.md)). In practice,
prefer `for`.

### Function definitions

```phonon
def teleport(qubit src, qubit anc, qubit dst) {
    h anc
    cx anc, dst
    cx src, anc
    h src
    bit m0[1]
    bit m1[1]
    m0 = measure src
    m1 = measure anc
    if (m1[0] == 1) { x dst }
    if (m0[0] == 1) { z dst }
}
```

Functions are inlined at compile time; there is no runtime call. No
recursion (mutual or otherwise).

### Variable assignment

`int` and `angle` parameters can be reassigned freely. `qubit` and
`bit` register operands cannot — they're *registers*, not variables.

```phonon
int n
n = 4                        ; OK (int)

qubit q[2]
qubit r[1]
r = q                        ; ERROR: registers are not values
```

## See also

- [Spinor grammar](../spinor/grammar.md) (the inherited base)
- [Linear types](linear_types.md)
- [Reference: `if`](reference/if.md), [`for`](reference/for.md), [`def`](reference/def.md)
