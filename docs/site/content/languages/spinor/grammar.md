# Spinor — grammar

The full grammar in EBNF. Twenty-two production rules. Read once
lightly, then read the walkthrough.

## Reading EBNF

| Notation | Meaning |
|---|---|
| `::=` | "is defined as" |
| `\|` | alternative (or) |
| `[ X ]` | X is optional |
| `{ X }` | X repeats zero or more times |
| `"x"` | literal text |

## The grammar

```
program       ::= target_decl { statement } ;

target_decl   ::= "target" target_id NEWLINE ;
target_id     ::= "generic" | identifier ;

statement     ::= declaration | gate_stmt | measure_stmt
                | reset_stmt | barrier_stmt | NEWLINE ;

declaration   ::= qubit_decl | bit_decl ;
qubit_decl    ::= "qubit" identifier "[" integer "]" NEWLINE ;
bit_decl      ::= "bit"   identifier "[" integer "]" NEWLINE ;

gate_stmt     ::= gate_name [ params ] operand { "," operand } NEWLINE ;
gate_name     ::= "h" | "x" | "y" | "z" | "s" | "sdg" | "t" | "tdg"
                | "rx" | "ry" | "rz" | "cx" | "cz" | "swap"
                | native_gate ;
native_gate   ::= "ecr" | "ms" | "rzz" | "sx" | "sxdg"
                | "gpi" | "gpi2" | "u1q" ;        (* HW contract only *)

params        ::= "(" angle { "," angle } ")" ;
angle         ::= real | [ "-" ] "pi" [ "/" integer ] | real "*" "pi" ;
operand       ::= identifier "[" integer "]" ;

measure_stmt  ::= operand "=" "measure" operand NEWLINE
                | identifier "=" "measure" identifier NEWLINE ;

reset_stmt    ::= "reset" operand NEWLINE ;
barrier_stmt  ::= "barrier" [ operand { "," operand } ] NEWLINE ;

comment       ::= ";" { ANY_CHAR } ;
identifier    ::= letter { letter | digit | "_" } ;
integer       ::= digit { digit } ;
real          ::= [ "-" ] digit { digit } [ "." digit { digit } ] ;
```

## Walkthrough — the five groups

### Program shape (3 rules)

Every Spinor file is one [`target`](reference/target.md) line followed
by statements. Nothing else.

### Declarations (3 rules)

`qubit q[2]` and `bit c[2]` introduce arrays of fixed size
([reference: qubit](reference/qubit_decl.md), [reference: bit](reference/bit_decl.md)).
The size is a literal integer — no expressions.

### Gate statements (5 rules)

```
gate_name [ params ] operand { "," operand }
```

A verb, optional angle parameters in parens, then comma-separated
operands. Examples:

```
h q[0]
rz(pi/2) q[3]
cx q[0], q[1]
```

The `native_gate` rule fires only under the [hardware
contract](rules/W6_hardware_contract.md). The
[`angle`](reference/rx.md#parameters) rule supports `pi`, `pi/N`,
`real`, `real*pi`, with optional unary minus.

### Measurement and friends (3 rules)

- [`c = measure q`](reference/measure.md) reads qubits into bits — one or
  whole arrays.
- [`reset q[i]`](reference/reset.md) returns a qubit to |0⟩ for reuse.
- [`barrier`](reference/barrier.md) is a fence — the optimizer cannot
  reorder across it.

### Lexical (4 rules)

What identifiers, integers, reals, and comments look like —
formalised in [lexical structure](lexical.md).

## Where each rule is enforced

- `program`, `target_decl`, `statement`: the parser
  [`Driver::parseProgram`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/parser/cpp/lib/Parser.cpp).
- `gate_stmt` validity (W3 distinct operands, W6 native gates):
  [`spinor::verify::Verifier`](https://github.com/nimesh08/quantum-stack/blob/main/spinor/verify/lib/Verifier.cpp).
- `qubit_decl` / `bit_decl` size > 0: parser, with diagnostic.

## See also

- [Lexical structure](lexical.md) — what the lexer produces
- [Types](types.md) — what `qubit q[N]` actually means
- Every `gate_stmt` lives on its own page under [reference/](index.md#reference-every-keyword-has-its-own-page).
