# Photon — grammar

Photon's grammar — the OO/kernel surface. Lower than Python, simpler
than full C++.

## Top-level

```ebnf
program     ::= [ target_decl ] { decl } ;
target_decl ::= "target" target_id ;
decl        ::= kernel_decl | def_decl ;

kernel_decl ::= "kernel" identifier "(" [ params ] ")"
                [ "->" return_type ] block ;

def_decl    ::= "def" identifier "(" [ params ] ")"
                [ "->" return_type ] block ;

params      ::= param { "," param } ;
param       ::= type identifier ;
return_type ::= "int" | "bit" "[" integer "]" ;
type        ::= "int" | "angle" | "bit" | "QReg" | "Oracle" ;
```

## Statements (inside a block)

```ebnf
block     ::= "{" { statement } "}" ;

statement ::= decl_stmt | gate_call | call_stmt
            | if_stmt | for_stmt | return_stmt
            | assign_stmt ;

decl_stmt   ::= "QReg" identifier "(" integer ")"
              | "int" identifier
              | "angle" identifier ;

gate_call   ::= identifier "." method_name "(" [ args ] ")" ;
method_name ::= "h" | "x" | … | "measure" | "measure_int" | "reset" ;

call_stmt   ::= identifier "(" [ args ] ")" ;

if_stmt     ::= "if" "(" condition ")" block [ "else" block ] ;
for_stmt    ::= "for" identifier "in" integer ".." integer block ;

return_stmt ::= "return" [ expression ] ;
assign_stmt ::= identifier "=" expression ;
```

## Expressions

```ebnf
expression ::= integer | real | identifier | identifier "[" expression "]"
             | identifier "." identifier "(" [ args ] ")"
             | expression "+" expression | expression "*" expression
             | "pi" | "pi" "/" integer | "(" expression ")" ;

condition  ::= expression ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) expression ;
args       ::= expression { "," expression } ;
```

## Walkthrough

### Kernel

```photon
kernel bell() -> int {
    QReg q(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()
}
```

A kernel is a function the runtime can execute. Marker-only — there's
no body of "kernel-only" syntax.

### `def` for helpers

```photon
def make_bell(QReg q, int a, int b) {
    q.h(a)
    q.cx(a, b)
}

kernel main() -> int {
    QReg q(2)
    make_bell(q, 0, 1)
    return q.measure_int()
}
```

Inlined at compile time.

### Library calls

```photon
kernel ghz3() -> int {
    QReg q(3)
    q.ghz()                  ; expands to h + chained cx
    return q.measure_int()
}
```

The seven `photon.lib` routines are method calls on `QReg`.

## See also

- [Lexical](lexical.md)
- [Types](types.md)
- [Reference: `kernel`](reference/kernel.md)
