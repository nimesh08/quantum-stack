# Phase C — M1: Photon language core

## 1. Goal & spec section

Bring up the **Photon language**: an OO surface with quantum
registers as objects (`QReg`), gates as methods (`q.h(0)`), and
measurement returning ordinary integers (`q.measure_int()`). Land
a `.pho` lexer + recursive-descent parser, a `photon` dialect that
sits on top of the existing Phonon dialect, and an OO-to-Phonon
lowering pass.

Spec section: Photon & Frontends Deep-Dive **Part 1 §§1–3**
(language, object model, worked example).

## 2. Inputs / outputs

**Input.** A `.pho` source file. Grammar (informal):

```ebnf
program     ::= header (def | kernel)*
header      ::= "target" target_id NEWLINE
              | (* optional; defaults to `generic` *)
target_id   ::= "generic" | IDENT
def         ::= "def" IDENT "(" params? ")" "->" type "{" stmts "}"
kernel      ::= "kernel" IDENT "(" params? ")" ("->" type)? "{" stmts "}"
type        ::= "int" | "angle" | "QReg" "(" INT ")" | IDENT
params      ::= param ("," param)*
param       ::= type IDENT
stmt        ::= var_decl | assignment | gate_call | lib_call
              | for_loop | if_stmt | return_stmt | measure_stmt
              | expr_stmt
var_decl    ::= type IDENT ("(" args? ")" | "=" expr)
gate_call   ::= IDENT "." gate_name "(" args? ")"
lib_call    ::= IDENT "." IDENT "(" args? ")"        (* photon.lib *)
measure_stmt::= IDENT "." ("measure" | "measure_int") "(" ")"
for_loop    ::= "for" IDENT "in" expr ".." expr "{" stmts "}"
if_stmt     ::= "if" "(" expr ")" "{" stmts "}" ("else" "{" stmts "}")?
return_stmt ::= "return" expr?
expr        ::= literal | IDENT | call | binop | paren | range
gate_name   ::= "h" | "x" | "y" | "z" | "s" | "sdg" | "t" | "tdg"
              | "rx" | "ry" | "rz" | "cx" | "cz" | "swap"
              | "sx" | "sxdg" | "ecr" | "ms" | "rzz"
              | "gpi" | "gpi2" | "u1q"
```

**Output.** A `phonon::dialect::Module` produced by the Photon
lowering pass. Every Photon kernel becomes a `phonon.def` whose
body is the Phonon-level expansion of the OO operations. The
module is then handed to Phonon's existing pipeline
(typecheck → optimize → lower → Phase A).

**Error contract.** Every parse and lowering error carries a
`Diagnostic` with file/line/column. Unrecognized constructs
produce a single message naming the construct (no cascades).

## 3. Deliverables

- `photon/lang/include/photon/lang/Lang.h` — public `Module`,
  `Diagnostics` re-exports.
- `photon/lang/include/photon/lang/Parser.h` — `parse(text,
  filename) -> ParseResult { Module, Diagnostics }`.
- `photon/lang/include/photon/lang/Lower.h` —
  `lowerToPhonon(const photon::Module&) -> phonon::dialect::Module`.
- `photon/lang/cpp/lib/Lexer.{h,cpp}` — token kinds and lexer.
- `photon/lang/cpp/lib/Parser.cpp` — recursive-descent parser.
- `photon/lang/cpp/lib/Module.cpp` — Photon AST data structures.
- `photon/lang/cpp/lib/Lower.cpp` — OO → Phonon lowering.
- `photon/lang/CMakeLists.txt` — wires `photon_lang` library.
- `photon/CMakeLists.txt` — re-enables `add_subdirectory(lang)`.
- `photon/tests/m1/{builder_test,parser_test,lower_test,e2e_test}.cpp`
  + `CMakeLists.txt`.
- `photon/tests/corpus/{bell.pho,ghz.pho,bell_kernel.pho,
  search_kernel.pho,for_loop.pho,if_else.pho}`.

## 4. Data structures

Photon AST (in `photon::lang`):

```cpp
struct Location { std::string file; uint32_t line, column; };

enum class TypeKind { QReg, Int, Angle, Bit, Void };
struct Type { TypeKind kind; uint32_t qreg_size = 0; };

enum class StmtKind {
  VarDecl, Assign, GateCall, LibCall, MeasureCall,
  ForLoop, IfStmt, ReturnStmt, ExprStmt
};

struct Expr { /* literal | ident | call | binop | range */ };
struct Stmt { StmtKind kind; /* + payload */ };

struct Param { Type type; std::string name; };
struct Function {
  std::string name;
  bool is_kernel = false;
  std::vector<Param> params;
  Type return_type;
  std::vector<Stmt> body;
  Location loc;
};

struct Module {
  std::string target = "generic";
  std::vector<Function> functions;
};
```

## 5. Algorithms

**Parsing** is hand-written recursive descent, mirroring the
Phonon parser style: one function per grammar production, a
single forward token pointer, and `Diagnostics`-driven error
recovery (skip-to-newline-or-`}`).

**Lowering** is a structured walk:

1. For each `Function f` in the Photon module:
   - Emit `phonon.def f(params...)`.
   - Walk body. Each `QReg(n)` declaration becomes a stretch of
     `phonon.qubit q_alloc[n]` allocations bound to a slot table
     keyed by the variable name.
   - Each `q.gate(args)` becomes the corresponding Phonon gate
     op on the slot the args resolve to (constant index or
     loop-variable expression).
   - Each `q.measure_int()` lowers to one `phonon.measure` per
     slot followed by a classical bit-pack expression
     (`bitN | bit{N-1} << 1 | ...`).
   - Each `q.measure()` returns a `bit[]` register (lowers to
     N parallel measures, kept as a Phonon `bit` register).
   - `for i in lo..hi { ... }` becomes `phonon.for var=i lo hi { ... }`
     unchanged (Phonon already supports counted loops).
   - `if (c) { ... } else { ... }` becomes `phonon.if pred { ... }
     phonon.else { ... }` — Phonon already handles this.
   - `lib.<name>(args)` lowers via the M2 expanders (M1 leaves a
     `phonon.call "lib.<name>" args` op in place; M2 fills in
     the body inlining).
2. Compile-time numeric arguments (rotation angles given as `pi/4`
   or constants) are folded by the parser before lowering.

**Slot table.** A `QReg(N)` declaration creates N Phonon qubit
SSA values stored in `slot[name][0..N-1]`. `q.h(i)` reads
`slot[name][i]` and writes the gate's result back to that slot.
This is the SSA-thread pattern Phonon's parser already uses.

## 6. Test matrix

| ID | Type | Inputs | Expected | CI gate |
| --- | --- | --- | --- | --- |
| M1.builder.basic | U | direct AST construction | round-trip equality | M1 |
| M1.builder.kernel | U | kernel with params | accepts QReg/int params | M1 |
| M1.parser.bell | U | `bell.pho` | AST shape matches expected | M1 |
| M1.parser.ghz | U | `ghz.pho` | AST shape matches expected | M1 |
| M1.parser.kernel | U | `bell_kernel.pho` | top-level `kernel` accepted | M1 |
| M1.parser.error | U | malformed `.pho` snippets | precise diagnostic per case | M1 |
| M1.parser.fold | U | `for i in 0..3` literal bounds | literal AST | M1 |
| M1.lower.bell | I | `bell.pho` | golden Phonon text | M1 |
| M1.lower.ghz | I | `ghz.pho` | golden Phonon text | M1 |
| M1.lower.for_loop | I | `for_loop.pho` | golden Phonon text | M1 |
| M1.lower.if_else | I | `if_else.pho` | golden Phonon text | M1 |
| M1.lower.measure_int | I | `bell.pho` w/ `measure_int` | bit-pack op present | M1 |
| M1.e2e.bell_through_phonon | E | `bell.pho` → photon → phonon → typecheck | typechecks clean | M1 |
| M1.e2e.bell_through_spinor | E | as above + lower to Spinor | flat Spinor module valid | M1 |
| M1.regression.containment_phn | R | every `.phn` corpus reparses unchanged | bit-stable | M1 |

## 7. Cookbook example

`photon/tests/corpus/bell.pho`:

```
target generic

kernel bell() -> int {
  QReg q(2)
  q.h(0)
  q.cx(0, 1)
  return q.measure_int()
}
```

After `photon::lang::lowerToPhonon`, the produced Phonon (printed
through the existing Phonon printer):

```
target generic
def bell() -> int {
  qubit q[2]
  bit c[2]
  h q[0]
  cx q[0], q[1]
  c[0] = measure q[0]
  c[1] = measure q[1]
  return (c[1] << 1) | c[0]
}
```

The Phonon module then runs unchanged through `phonon::types::typecheck`,
`phonon::optimizer::runPipeline`, and `phonon::lower::lower`.

## 8. Pitfalls

- **Slot threading after `cx`.** A `cx` returns two new SSA
  qubit values; both slots must be updated, in argument order.
  Caught by `M1.lower.bell` — the lowered Phonon must
  successfully pass `phonon::types::typecheck`, which would
  reject any qubit reuse.
- **`measure_int` bit-order.** Spec: low-numbered qubit is the
  least-significant bit. Caught by `M1.lower.measure_int`.
- **Empty kernel.** A kernel with no body (`kernel x() {}`) must
  parse and lower to an empty `phonon.def`. Caught by
  `M1.parser.error` (positive case) and `M1.lower.bell` (no
  crash on empty body).
- **`#` line comments inside method calls.** Phonon's `;`
  comment is preserved; Photon also supports `//` and `/* */`
  C-family comments to feel natural to the C++ frontend
  audience. Lexer must handle both.
- **`pi` in argument lists.** Photon accepts `q.rz(pi/4, 0)`;
  the parser must fold `pi/4` to a real literal at parse time
  to match Phonon's `phonon.rz` op shape.
- **Unknown lib calls.** `q.something_unknown(...)` must
  produce a precise "no such method on QReg" error rather than
  silently emitting an unknown Phonon call. M1 leaves
  `lib.<name>` calls as `phonon.call`; M2 will resolve them.

## 9. Definition of Done

- [ ] `photon/lang/` builds.
- [ ] All `M1.*` tests pass.
- [ ] `bell.pho` lowers to the cookbook Phonon text byte-for-byte
      (golden test).
- [ ] `bell.pho`'s lowered Phonon passes `phonon::types::typecheck`,
      `phonon::optimizer::runPipeline`, and `phonon::lower::lower`
      to a flat Spinor module that Phase A's verifier accepts.
- [ ] `phaseC/test-matrix.md` updated with M1 rows.
- [ ] `phaseC_progress.md` appended.

## 10. Open questions

None blocking. (Library expansion lands in M2.)
