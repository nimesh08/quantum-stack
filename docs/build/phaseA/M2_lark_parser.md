# M2 — Lark prototype parser (throwaway Python)

## 1. Goal & spec section

A Python parser using Lark (LALR(1)) that reads `.spn` source
text and produces a JSON intermediate that our C++ ingest tool
consumes to build a `spinor::dialect::Module`. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 1 §3 (the
22-rule grammar) and §4.2 (Lark prototype parser).

By Rule 3, this code is throwaway: it lives under
`spinor/parser/lark/` and is deleted at M7 once the C++
recursive-descent parser is in place.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:** `.spn` source text (UTF-8).
- **Produces:**
  - A Python AST (Lark `Tree`) used by tests to assert grammar
    correctness.
  - A JSON-IR document — a list of ops with their operands,
    attributes, and types — that mirrors the Spinor dialect
    one-to-one.
  - A small CLI: `python -m spinor.parser.lark FILE.spn` →
    JSON on stdout.
  - A C++ tool, `spinorc-ingest`, that reads the JSON and
    produces a printed Spinor IR (using M1's printer). This
    closes the round-trip: text → JSON → IR → printed IR.
- **Invariants on output:** every op references operands by
  the names declared at the source level (e.g. `q[0]`); the
  ingest tool resolves these to ValueIds, and the printed IR
  is structurally identical to one produced by hand-built
  Builder calls.

## 3. Deliverables

- `spinor/parser/lark/spinor/__init__.py`
- `spinor/parser/lark/spinor/grammar.lark` — the EBNF grammar
  in Lark form (mirrors Deep-Dive §3.1 line for line).
- `spinor/parser/lark/spinor/parser.py` — Lark parser +
  Transformer that emits the JSON-IR.
- `spinor/parser/lark/spinor/__main__.py` — CLI entry point.
- `spinor/parser/lark/pyproject.toml` — pin Lark `==1.3.1`
  (verified 2026-06-16).
- `spinor/parser/lark/tests/test_grammar.py` — pytest cases
  on the corpus.
- `spinor/parser/lark/tests/corpus/{bell,ghz,rotations,mid_circuit,native_ibm,native_ionq,malformed_*}.spn`
- `spinor/cli/CMakeLists.txt`
- `spinor/cli/ingest_main.cpp` — `spinorc-ingest` (consumes
  the JSON-IR, builds the dialect Module, prints).

## 4. Data structures

### JSON-IR

```jsonc
{
  "target": "generic" | "<device-id>",
  "name": "main",
  "ops": [
    { "kind": "alloc_qubit", "name": "q0", "loc": [line, col] },
    { "kind": "alloc_bit",   "name": "c0", "loc": [...] },
    { "kind": "h",           "operands": ["q[0]"], "loc": [...] },
    { "kind": "rx",          "operands": ["q[0]"], "attrs": {"angle": 0.5}, "loc": [...] },
    { "kind": "cx",          "operands": ["q[0]", "q[1]"], "loc": [...] },
    { "kind": "measure",     "operands": ["q[0]"], "writes": "c[0]", "loc": [...] },
    { "kind": "reset",       "operands": ["q[0]"], "loc": [...] },
    { "kind": "barrier",     "operands": ["q[0]", "q[1]"], "loc": [...] }
  ],
  "regs": {
    "q": { "kind": "qubit", "size": 2 },
    "c": { "kind": "bit",   "size": 2 }
  }
}
```

### Lark grammar

Direct transcription of Deep-Dive §3.1. Three lexer
specialisations:

- `_NL`: `\r?\n` — declared in the grammar's terminal section
  with `%ignore` for whitespace inside lines but not for
  newlines (Spinor is a line-oriented language).
- `COMMENT`: `;[^\n]*` — `%ignore`d.
- `ANGLE_PI`: matches `pi`, `-pi`, `pi/N`, `-pi/N`, `R*pi`
  forms.

## 5. Algorithms

- **Lark parser, LALR(1).** Lark's grammar is essentially the
  EBNF, so translation is mechanical. The grammar in
  `grammar.lark` is the single source of truth for the
  Python prototype; the C++ recursive-descent parser (M7) is
  a hand transcription of the same grammar.
- **Transformer.** For every grammar non-terminal that maps to
  a JSON-IR op, a corresponding `transformer` method emits a
  dict. The transformer carries a register table to translate
  `qubit q[2]` declarations into `regs` entries.
- **Angle evaluation.** `pi/2` etc. are evaluated to `double`
  at parse time using `math.pi`, so the JSON-IR carries
  numeric angles only.
- **Ingest tool.** Walks the JSON-IR, allocates qubit/bit
  values via the M1 Builder API, resolves operand names to
  ValueIds (per-register live-value table), and emits ops.

## 6. Test matrix

| ID    | Name                                | Type        | Inputs                          | Expected output                                    | CI gate           |
| ----- | ----------------------------------- | ----------- | ------------------------------- | -------------------------------------------------- | ----------------- |
| M2-T01 | grammar.bell_parses                | unit        | corpus/bell.spn                 | parses; JSON-IR has 8 ops (2 alloc_q, 2 alloc_b, h, cx, 2 measure) | `ctest -L M2`     |
| M2-T02 | grammar.ghz_parses                 | unit        | corpus/ghz.spn                  | parses; JSON-IR has 9 ops                          | `ctest -L M2`     |
| M2-T03 | grammar.rotations_parses           | unit        | corpus/rotations.spn            | parses; angles `pi/2`, `-pi`, `0.5*pi` evaluate    | `ctest -L M2`     |
| M2-T04 | grammar.native_ibm_parses          | unit        | corpus/native_ibm.spn           | `ecr`, `sx`, `rz` accepted under `target ibm_heron_r2` | `ctest -L M2` |
| M2-T05 | grammar.native_ionq_parses         | unit        | corpus/native_ionq.spn          | `ms`, `gpi`, `gpi2` accepted under `target ionq_forte` | `ctest -L M2` |
| M2-T06 | grammar.mid_circuit_parses         | unit        | corpus/mid_circuit.spn          | parses; mid-circuit `measure` and `reset`         | `ctest -L M2`     |
| M2-T07 | grammar.malformed_no_target        | regression  | corpus/malformed_no_target.spn  | parser rejects with a precise error                | `ctest -L M2`     |
| M2-T08 | grammar.malformed_bad_gate         | regression  | corpus/malformed_bad_gate.spn   | parser rejects                                     | `ctest -L M2`     |
| M2-T09 | grammar.malformed_unbalanced_bracket | regression | corpus/malformed_brackets.spn  | parser rejects                                     | `ctest -L M2`     |
| M2-T10 | ingest.bell_round_trip             | integration | corpus/bell.spn                 | text → JSON-IR → built Module → printed IR matches M1 bell golden | `ctest -L M2` |
| M2-T11 | ingest.ghz_round_trip              | integration | corpus/ghz.spn                  | matches M1 ghz golden                              | `ctest -L M2`     |
| M2-T12 | ingest.rotations_round_trip        | integration | corpus/rotations.spn            | matches M1 rotations golden                        | `ctest -L M2`     |
| M2-T13 | ingest.via_cli                     | e2e         | `spinorc-ingest bell.spn`       | exits 0; stdout matches M1 bell golden             | `ctest -L M2`     |

## 7. Cookbook example — `bell.spn` end-to-end

Source file `bell.spn`:

```
target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
```

Step 1: Lark parses to a `Tree`. Step 2: the Transformer
emits JSON-IR (abbreviated):

```json
{ "target": "generic",
  "regs": {"q": {"kind":"qubit","size":2}, "c": {"kind":"bit","size":2}},
  "ops": [
    {"kind":"alloc_qubit","name":"q0","loc":[2,1]},
    {"kind":"alloc_qubit","name":"q1","loc":[2,1]},
    {"kind":"alloc_bit","name":"c0","loc":[3,1]},
    {"kind":"alloc_bit","name":"c1","loc":[3,1]},
    {"kind":"h","operands":["q[0]"],"loc":[4,1]},
    {"kind":"cx","operands":["q[0]","q[1]"],"loc":[5,1]},
    {"kind":"measure","operands":["q[0]"],"writes":"c[0]","loc":[6,1]},
    {"kind":"measure","operands":["q[1]"],"writes":"c[1]","loc":[6,1]}
  ]
}
```

Step 3: `spinorc-ingest` reads the JSON, calls the Builder
API, prints the resulting Module. The output is exactly
`spinor/tests/m1/goldens/bell.spnir` — proving the
text→JSON→IR pipeline is consistent with the hand-built
reference.

## 8. Pitfalls

- **Newlines as terminators.** Spinor is line-oriented but
  Lark's default behaviour ignores all whitespace. We must
  declare `_NL` explicitly and propagate it through every
  `statement` rule. Pitfall: a missing trailing newline
  produces a confusing "unexpected EOF" — we provide a
  one-line tail-newline tolerance in the lexer, but if the
  test fails it's almost always this.
- **Comment regex.** `;[^\n]*` must not include the newline,
  or the line terminator gets eaten. M2-T07/08/09 cover edge
  cases.
- **`measure q` vs `c = measure q`.** Two syntactic forms; the
  transformer normalises both to per-element ops in the
  JSON-IR.
- **Angle format.** `pi/2` is parsed as a fraction; `0.5*pi`
  is parsed as a multiplier. Both evaluate at parse time. The
  C++ parser in M7 must implement the same evaluation; M2
  goldens lock in the numeric values.
- **Throwaway warning.** This Python code path is deleted at
  M7; do not import it from C++ code or ship it as a runtime
  dependency.

## 9. Definition of Done

- [x] Spec landed.
- [ ] All M2 unit/regression/integration/e2e tests green.
- [ ] `pyproject.toml` pins Lark `==1.3.1` (verified
      2026-06-16).
- [ ] Corpus has the 9 listed `.spn` files.
- [ ] `spinorc-ingest` round-trip produces byte-identical
      output to the M1 goldens for `bell.spn`, `ghz.spn`,
      `rotations.spn`.
- [ ] Test matrix updated.
- [ ] Progress journal appended.

## 10. Open questions

None. The grammar is fully specified; all I need to do is
transcribe it into Lark form and write the transformer.
