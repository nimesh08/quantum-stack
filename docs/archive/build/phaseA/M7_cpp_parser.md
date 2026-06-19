# M7 — Production C++ Recursive-Descent Parser

## 1. Goal & spec section

Replace the Lark prototype (M2) with a hand-written
recursive-descent parser that produces a `dialect::Module`
directly, in C++, with no external runtime dependency.
Implements [Spinor Engineering Deep-Dive][deep-dive] Part 1
§4.3.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:** `.spn` source text (UTF-8) plus an optional
  filename for diagnostics.
- **Produces:** a `dialect::Module` (or a `Diagnostics` with
  parse errors).
- **Invariants:** AST equivalent to what the Lark prototype
  produced for every file in the M2 corpus — proven by a
  parity test that compares the printed Modules byte-for-byte.

## 3. Deliverables

- `spinor/parser/cpp/include/spinor/parser/Parser.h`
- `spinor/parser/cpp/lib/Lexer.h` + `Lexer.cpp`
- `spinor/parser/cpp/lib/Parser.cpp` — one parse function per
  grammar rule.
- `spinor/parser/cpp/CMakeLists.txt`
- `spinor/cli/spinorc_main.cpp` — full `spinorc` CLI driver
  (`parse`, `verify`, `compile` subcommands using M1–M6).
- `spinor/tests/m7/CMakeLists.txt`
- `spinor/tests/m7/parser_test.cpp` — parses every corpus
  file; rejects every malformed file with a precise error.
- `spinor/tests/m7/parity_test.cpp` — for each corpus file,
  Lark-built printed Module == C++-built printed Module.
- `spinor/tests/m7/error_message_test.cpp` — golden error
  messages with line/column.

The Lark prototype is **not** deleted in M7 — it stays around
as a regression reference for M11 (OpenQASM import) and is
removed at end of Phase A.

## 4. Data structures

```c++
namespace spinor::parser {

struct ParseResult {
  std::optional<dialect::Module> module;
  dialect::Diagnostics diag;
};

ParseResult parse(std::string_view text,
                  std::string_view filename = "<input>");

}  // namespace spinor::parser
```

Internal: a `Lexer` producing a stream of `Token{kind, text,
line, column}` and a `Parser` with one `parse<Rule>` per
grammar non-terminal, mirroring the EBNF in §3.1.

## 5. Algorithms

Standard recursive descent. One function per grammar rule,
each consuming tokens and calling the others. Errors throw a
local `ParseError{loc, message}`; the entry point catches and
appends to `Diagnostics`.

Angle evaluation matches the Lark prototype byte-for-byte:
`pi/N` → `M_PI / N`, `-pi/N` → `-M_PI / N`, `pi` → `M_PI`,
`-pi` → `-M_PI`, `R*pi` → `R · M_PI`, raw decimal → `stod`.

## 6. Test matrix

| ID    | Name                          | Type        | Inputs                                 | Expected                                                | CI gate |
| ----- | ----------------------------- | ----------- | -------------------------------------- | ------------------------------------------------------- | ------- |
| M7-T01 | `M7_parse.bell`              | unit        | bell.spn                               | parses; module has the expected ops                     | `ctest -L M7` |
| M7-T02 | `M7_parse.ghz`               | unit        | ghz.spn                                | parses; expected ops                                    | `ctest -L M7` |
| M7-T03 | `M7_parse.rotations`         | unit        | rotations.spn                          | angles match Lark byte-for-byte                          | `ctest -L M7` |
| M7-T04 | `M7_parse.native_ibm`        | unit        | native_ibm.spn                         | parses                                                   | `ctest -L M7` |
| M7-T05 | `M7_parse.native_ionq`       | unit        | native_ionq.spn                        | parses                                                   | `ctest -L M7` |
| M7-T06 | `M7_parse.mid_circuit`       | unit        | mid_circuit.spn                        | parses                                                   | `ctest -L M7` |
| M7-T07 | `M7_parse.malformed_no_target` | regression | malformed_no_target.spn                | parser rejects with line 3 reference                     | `ctest -L M7` |
| M7-T08 | `M7_parse.malformed_bad_gate` | regression | malformed_bad_gate.spn                 | parser rejects with helpful "unknown gate"               | `ctest -L M7` |
| M7-T09 | `M7_parse.malformed_brackets` | regression | malformed_brackets.spn                 | parser rejects with "expected ']'"                       | `ctest -L M7` |
| M7-T10 | `M7_parity.bell`             | integration | bell.spn                               | print(LarkBuild) == print(CxxBuild)                      | `ctest -L M7` |
| M7-T11 | `M7_parity.ghz`              | integration | ghz.spn                                | identical                                                 | `ctest -L M7` |
| M7-T12 | `M7_parity.rotations`        | integration | rotations.spn                          | identical                                                 | `ctest -L M7` |

## 7. Cookbook example — Bell, two ways

```cpp
ParseResult r = spinor::parser::parse(slurp("bell.spn"));
if (r.module) {
  std::cout << print(*r.module);
}
```

Output is byte-identical to running the Lark prototype +
ingest tool from M2.

## 8. Pitfalls

- **Tokenizer greed.** `IDENT` and `GATE_NAME` need careful
  boundary handling; the Lark prototype uses priorities. In
  C++ we hard-code the gate-name table at the start of an op
  position.
- **Newlines.** Spinor is line-oriented; tokens are split by
  `\n`. The lexer emits explicit `NEWLINE` tokens.
- **Negative angles.** `-pi`, `-pi/2`, `-1.5` all parse;
  `-1.5*pi` is a single `angle_mul_pi`.

## 9. Definition of Done

- [ ] Spec landed.
- [ ] All M7-T## tests pass.
- [ ] Parity holds on every M2 corpus file.
- [ ] `spinorc` CLI builds and runs the full pipeline.
- [ ] Test matrix updated.
- [ ] Progress journal appended.

## 10. Open questions

None.
