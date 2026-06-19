# M2 — Phonon parser

## 1. Goal & spec section

Hand-written recursive-descent parser for Phonon source (`.phn`),
implementing Phonon Deep-Dive **Part 1 §2**. Strict superset of
Spinor's grammar: every Phase A `.spn` file parses unchanged.

## 2. Inputs / outputs

- **Input.** UTF-8 text plus a filename for diagnostics.
- **Output.** A `phonon::dialect::Module` plus `Diagnostics`.

## 3. Deliverables

- `phonon/parser/cpp/include/phonon/parser/Parser.h` — public API
  matching Spinor's `parser::ParseResult` shape.
- `phonon/parser/cpp/lib/Lexer.{h,cpp}` — Phonon lexer; superset of
  the Spinor token set with `if`, `else`, `for`, `while`, `def`,
  `return`, `int`, `angle`, `in`, `..`, `{`, `}`, `<`, `>`,
  `==`, `!=`, `+`, `-`, `*`, `/`, `<=`, `>=`.
- `phonon/parser/cpp/lib/Parser.cpp` — recursive-descent parser.
- `phonon/parser/CMakeLists.txt` (rewritten with real targets).
- Tests under `phonon/tests/m2/`:
  - **Containment.** Every fixture in `spinor/tests/corpus/`
    parses through the Phonon parser successfully.
  - **Structured corpus.** New `.phn` fixtures (teleportation,
    qft_loop, bell_pair_func) parse and produce IR with the
    expected control-flow ops.
  - **Error messages.** Malformed inputs produce line/column
    diagnostics.

## 4. Data structures

`phonon::parser::ParseResult` mirrors `spinor::parser::ParseResult`
but for the Phonon module.

The lexer extends `Tok` with: `If, Else, For, While, Def, Return,
Int, Angle, In, DotDot, LBrace, RBrace, Lt, Gt, Le, Ge, Plus,
EqEq, NotEq`. Most existing tokens (`Target`, `Generic`, `Qubit`,
`Bit`, `Measure`, `Reset`, `Barrier`, `Pi`, `GateName`,
`Identifier`, `Integer`, `Real`, `LBracket`, `RBracket`, `LParen`,
`RParen`, `Comma`, `Equals`, `Star`, `Slash`, `Minus`, `Newline`,
`Eof`) are reused with the same meaning.

## 5. Algorithms

Top-level grammar (EBNF, mirrors the Phonon Deep-Dive Part 1 §2):

```
program     ::= header { stmt }
header      ::= "target" ( "generic" | identifier ) NEWLINE
stmt        ::= decl_stmt
              | gate_stmt
              | measure_stmt
              | reset_stmt
              | barrier_stmt
              | if_stmt
              | for_stmt
              | while_stmt
              | def_stmt
              | call_stmt
              | assign_stmt
              | return_stmt
decl_stmt   ::= ("qubit" | "bit") IDENT "[" Integer "]" NEWLINE
              | ("int" | "angle") IDENT "=" expr NEWLINE       (* phonon-only *)
gate_stmt   ::= GATE_NAME [ "(" expr_list ")" ] qubit_ref { "," qubit_ref } NEWLINE
measure_stmt::= IDENT "=" "measure" qubit_ref NEWLINE
reset_stmt  ::= "reset" qubit_ref NEWLINE
barrier_stmt::= "barrier" qubit_ref { "," qubit_ref } NEWLINE
if_stmt     ::= "if" "(" cond ")" block [ "else" block ]
for_stmt    ::= "for" IDENT "in" expr ".." expr block
while_stmt  ::= "while" "(" cond ")" block
def_stmt    ::= "def" IDENT "(" [ param { "," param } ] ")" block
param       ::= ("qubit" | "bit" | "int" | "angle") IDENT
call_stmt   ::= IDENT "(" [ expr { "," expr } ] ")" NEWLINE
assign_stmt ::= IDENT "=" expr NEWLINE
return_stmt ::= "return" [ expr { "," expr } ] NEWLINE
block       ::= "{" { stmt } "}"
cond        ::= expr ("==" | "!=" | "<" | ">" | "<=" | ">=") expr
expr        ::= term { ("+" | "-") term }
term        ::= factor { ("*" | "/") factor }
factor      ::= INT | REAL | IDENT | IDENT "[" expr "]"
              | "pi" | "(" expr ")" | "-" factor
qubit_ref   ::= IDENT [ "[" expr "]" ]
expr_list   ::= expr { "," expr }
```

Compile-time constant folding: where the bound of a `for` loop
or the `[N]` in a `qubit q[N]` declaration uses an integer expression,
the parser evaluates the expression eagerly (using a tiny
constant-folder over `+ - * /` and `pi`). If folding fails, the
parser emits an error.

Parser state:

- `symbols` — name→ValueId table for `qubit`, `bit`, `int`,
  `angle`, parameters; supports re-binding (`assign`).
- Outer module-level qubit registers (`qubit q[N]`) lower to N
  `alloc_qubit` ops bound to `q[0]..q[N-1]`. Each subsequent
  reference to `q[i]` uses the current latest value of that slot
  (Spinor-style SSA threading); a gate write updates the slot.
- Functions are stored as parser-side templates (name, params,
  body token range) and emitted as `phonon.def`/`phonon.call`
  ops with the body emitted into the module exactly once at the
  def site.

## 6. Test matrix

| Test name | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M2_contain.bell_spn` | integration | `spinor/tests/corpus/bell.spn` | parses; produces a Phonon module with the same op count as the Spinor parser would. |
| `M2_contain.ghz_spn` | integration | `spinor/tests/corpus/ghz.spn` | parses; ops match. |
| `M2_contain.rotations_spn` | integration | `spinor/tests/corpus/rotations.spn` | parses. |
| `M2_contain.native_ibm_spn` | integration | `spinor/tests/corpus/native_ibm.spn` | parses. |
| `M2_contain.native_ionq_spn` | integration | `spinor/tests/corpus/native_ionq.spn` | parses. |
| `M2_contain.mid_circuit_spn` | integration | `spinor/tests/corpus/mid_circuit.spn` | parses. |
| `M2_struct.teleportation` | integration | `phonon/tests/corpus/teleportation.phn` | parses; module contains exactly two `phonon.if` ops and a `phonon.cmp`. |
| `M2_struct.bell_pair_func` | integration | `phonon/tests/corpus/bell_pair_func.phn` | parses; one `phonon.def` + two `phonon.call`. |
| `M2_struct.qft_loop` | integration | `phonon/tests/corpus/qft_loop.phn` | parses; one `phonon.for`. |
| `M2_error.unbalanced_brace` | unit | malformed | error message includes line/column. |
| `M2_error.unknown_func` | unit | calls undefined fn | clear error. |

## 7. Cookbook example

`bell_pair_func.phn`:

```
target generic
qubit q[4]
bit c[4]

def bell_pair(qubit a, qubit b) {
  h a
  cx a, b
}

bell_pair(q[0], q[1])
bell_pair(q[2], q[3])
c = measure q
```

The parser produces a module containing:

- 4 `spinor.alloc_qubit` ops, 4 `spinor.alloc_bit` ops.
- One `phonon.def` op named "bell_pair" with two `qubit`
  parameters.
- The body of `bell_pair`: `spinor.h`, `spinor.cx`.
- One `phonon.end_def`.
- Two `phonon.call` ops to "bell_pair".
- Four `spinor.measure` ops.

## 8. Pitfalls

- **Grammar collisions with Spinor.** `bit c[2]` is a Spinor
  declaration; we must not let the Phonon parser interpret `bit`
  as a type-prefix on a parameter at the wrong level. Resolution:
  the parser uses lookahead; `bit` as a statement-leading token
  is only a parameter when inside a `(` after `def`.
- **`int`/`angle` as both keyword and type-prefix.** Our lexer
  treats them as keywords; the parser's stmt dispatcher checks
  context.
- **Function call vs. assignment.** `bell_pair(q[0], q[1])` is a
  call statement; `i = 5` is an assignment. The parser uses
  one-token lookahead on `(` vs. `=`.
- **Compile-time bounds.** `for i in 0..N`: if `N` is an integer
  literal we use it; if it's a previously-declared `int` constant
  we resolve it; otherwise emit an error pointing at the hi-bound
  expression.
- **Measurement assignment.** Spinor's `c = measure q` (whole
  register) lowers to N `spinor.measure` ops; `c[i] = measure
  q[i]` is also accepted.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M2`.
- [ ] Phase A test corpus (`spinor/tests/corpus/*.spn`) parses
      verbatim through the Phonon parser.
- [ ] Phase A tests remain green (containment must not break
      Phase A).
- [ ] `phaseB_progress.md` entry appended.

## 10. Open questions

None.
