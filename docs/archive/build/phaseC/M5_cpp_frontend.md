# Phase C — M5: C++ Clang LibTooling frontend

## 1. Goal & spec section

A C++ ingester: read a `[[photon::kernel]]`-marked function out of
an ordinary `.cpp` file, walk its body, and emit Phonon. Driven by
a YAML build config. Hands the resulting Phonon to the engine.

Spec section: Photon & Frontends Deep-Dive **Part 2 §6**.

## 2. Inputs / outputs

**Build config** (YAML):

```yaml
build:
  sources: [kernels.cpp]
  entry: bell_kernel
  target: quantinuum_helios
  shots: 2000
```

**C++ source** (the contract):

```cpp
[[photon::kernel]]
int bell_kernel() {
  QReg q(2);
  q.h(0);
  q.cx(0, 1);
  return q.measure_int();
}
```

**Output.** A `phonon::dialect::Module` that the existing engine
(typecheck → optimize → lower → Spinor) compiles unchanged. Plus
the same `CompiledProgram` shape M3 ships.

## 3. Deliverables

- `photon/frontends/cpp/include/photon/cppfront/Ingest.h` —
  public `ingestCpp(source, entry) -> photon::lang::Module`.
- `photon/frontends/cpp/lib/Ingest.cpp` — the ingester. Two paths:
  - **LibTooling path** (`#ifdef PHOTON_HAVE_CLANG`): runs a
    Clang `FrontendAction` + `RecursiveASTVisitor` to produce
    Phonon. Wired but not built by default.
  - **Self-hosted path** (default): a recursive-descent
    mini-parser for the C++ subset Photon kernels can use
    (`[[photon::kernel]]`, `int`/`double` parameters, `QReg`
    declarations, `q.<gate>(args)` calls, `for` /
    `if`-on-`measure`, `return q.measure_int()`).
- `photon/frontends/cpp/cli/Photonc.cpp` — `photonc-cxx` driver:
  reads the YAML config, calls the ingester, calls the engine,
  prints the resource estimate.
- `photon/frontends/cpp/CMakeLists.txt`.
- `photon/tests/m5/{ingest_test.cpp, photonc_cli_test.cpp}` +
  `CMakeLists.txt`.
- Sample kernels under `photon/tests/corpus/cpp/{bell.cpp,
  ghz.cpp, lib_grover.cpp}`.

## 4. Data structures

```cpp
namespace photon::cppfront {

struct IngestResult {
  std::optional<photon::lang::Module> module;
  photon::lang::Diagnostics diag;
};

IngestResult ingestCpp(std::string_view source,
                       std::string_view entry,
                       std::string_view target = "generic");

}  // namespace photon::cppfront
```

Note that we re-use `photon::lang::Module` — the C++ ingester is
a *peer* of the `.pho` parser, not a parallel pipeline. Both
produce the same Photon AST; both then go through
`photon::lang::lowerToPhonon`.

## 5. Algorithms

**Self-hosted path.** The mini-parser:

1. Tokenize the source. Tokens: keywords (`int`, `double`,
   `void`, `for`, `if`, `else`, `return`, `QReg`, `auto`),
   punctuation (`{}();,.[]`), identifiers, integer/real literals,
   `[[`, `]]`, operators (`==`, `=`, `+`, `-`, `*`, `/`, `<<`,
   `>>`).
2. Walk top-level. Skip `#include`, `using`, `namespace`. Match
   each function declaration.
3. Look for the `[[photon::kernel]]` attribute on the function;
   if absent, skip.
4. If the function name matches the requested `entry`, read the
   body and dispatch on the same statement kinds as the `.pho`
   parser:
   - `QReg name(N);` → VarDecl QReg.
   - `name.method(args);` → GateCall / LibCall / Measure*.
   - `for (int i = lo; i < hi; ++i) { ... }` → ForLoop.
   - `if (c == 1) { ... } else { ... }` → IfStmt.
   - `return name.measure_int();` → ReturnStmt with measure.
5. Anything outside this subset is rejected with a precise
   diagnostic referring to the C++ source line.

The self-hosted path is *not* a complete C++ parser. It only
recognises kernel-bodied subset constructs; everything else is a
diagnostic. This is the right scope for a thin frontend (RULE 3).

**LibTooling path** (sketch, gated on `PHOTON_HAVE_CLANG`):

```cpp
class KernelVisitor : public clang::RecursiveASTVisitor<KernelVisitor> {
 public:
  bool VisitFunctionDecl(clang::FunctionDecl* fd) {
    if (!isMarkedKernel(fd)) return true;       // attribute check
    walkBody(fd->getBody());
    return true;
  }
  // statement / expression visitors mirror the self-hosted ones
  // but consume Clang AST nodes.
};

class KernelAction : public clang::ASTFrontendAction { ... };
```

Activated only when the builder has Clang headers and library on
the system; CI runs on the self-hosted path.

## 6. Test matrix

| ID | Type | Inputs | Expected | Gate |
| --- | --- | --- | --- | --- |
| M5.ingest.bell | U | `bell.cpp` | Photon module with kernel `bell_kernel` | M5 |
| M5.ingest.ghz | U | `ghz.cpp` | gate counts match catalogue | M5 |
| M5.ingest.lib_grover | U | `lib_grover.cpp` | grover lib expansion path | M5 |
| M5.ingest.no_kernel | U | source w/o `[[photon::kernel]]` | precise diagnostic | M5 |
| M5.ingest.unknown_construct | U | C++ feature outside subset | diagnostic | M5 |
| M5.cli.photonc_cxx_compile | I | YAML config + bell.cpp | resource estimate printed | M5 |
| M5.engine.cpp_to_phonon_to_spinor | E | bell.cpp end-to-end | Spinor dump non-empty | M5 |

## 7. Cookbook example

```yaml
# build.yaml
build:
  sources: [bell.cpp]
  entry: bell_kernel
  target: generic
  shots: 1024
```

```bash
$ photonc-cxx build.yaml
phonon (first 80 chars): 'phonon.module @main attributes ...'
spinor (first 80 chars): 'spinor.module @main ...'
estimate: { num_qubits=2, two_qubit_count=1, t_count=0, depth=4 }
```

## 8. Pitfalls

- **Attribute syntax compatibility.** Some C++ codebases use the
  GCC/Clang `__attribute__((annotate("photon-kernel")))` form
  instead of `[[photon::kernel]]`. The self-hosted path accepts
  both; LibTooling matches them via attribute matchers.
- **`auto` and templates.** Out of M5 scope. The diagnostic for
  `auto` is precise and points at the `auto` token.
- **`#include "photon.hpp"`.** The header exists only as a
  translator hint; the ingester ignores all `#include` directives.
- **Comments and macros.** The lexer strips C++ `//` and `/* */`
  comments; macro expansion is *not* performed (no preprocessor).
  This is a known limitation of the self-hosted path; LibTooling
  performs full preprocessing.
- **Missing `;` at end of statement.** The self-hosted parser
  produces a clear diagnostic at the bad token rather than
  cascading.

## 9. Definition of Done

- [ ] All M5 tests pass.
- [ ] A marked C++ kernel compiles to Phonon and runs through the
      engine.
- [ ] Diagnostics are precise (line/column) for every rejection
      mode.
- [ ] `phaseC/test-matrix.md` updated.
- [ ] `phaseC_progress.md` appended.

## 10. Open questions

- LibTooling integration is wired behind `PHOTON_HAVE_CLANG` but
  not exercised by CI (no LLVM on the build host). **Resolution:
  document as Phase D follow-up.**
