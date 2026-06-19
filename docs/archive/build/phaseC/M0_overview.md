# Phase C (Photon and the Frontends) — M0 overview

Photon is the **top** layer of the Photon · Phonon · Spinor stack:
the developer-facing OO language and its standard library
(`photon.lib`), plus the **Python `@photon.kernel` decorator** and
the **C++ Clang-LibTooling ingester** that translate ordinary
Python and C++ functions into Phonon IR.

The architectural rule for this phase, repeated everywhere:

> **Photon and both frontends are THIN translators into Phonon —
> front doors, not parallel buildings.** They each produce a
> Phonon module and hand it off to the Phonon→Spinor→chip engine
> already built in Phases A and B. They never reimplement
> compilation.

```
.pho source                          Python @photon.kernel function
     │                                              │
     │ (photon/lang)                                │ (frontends/python)
     │                                              │
     │  .py @photon.kernel ──── inspect.getsource ──┤
     │  .cpp marked kernel ──── Clang LibTooling ───┤
     │                                              │
     ▼                                              ▼
              Phonon IR (Phase B dialect)
                       │
                       │  type-check (M3)  →  optimize (M5+M6)  →  lower (M4)
                       ▼
              flat Spinor IR  (Phase A)
                       │
                       │  placement → routing → decompose → emit → submit
                       ▼
                   provider hardware
```

## Three on-ramps

- **From quantum.** Skip §1 below. Photon is what your program is
  written in once Phonon and Spinor exist underneath it. Read §2
  (the OO surface) and §4 (the convergence story).
- **From compilers.** Three frontends, one IR. Photon's `.pho`
  parser is a recursive-descent C++ ingester producing the existing
  Phonon dialect; the Python frontend is `inspect.getsource` →
  `ast.parse` → visitor; the C++ frontend is a Clang LibTooling
  `FrontendAction` running a `RecursiveASTVisitor`. Read §1 (the
  thin-translator discipline) and §3 (the nanobind boundary).
- **From neither.** Read §1 → §2 → §3 → §4 in order.

The full glossary lives in [`glossary.md`](glossary.md).

## Dependency graph

```
                            M1  Photon language core
                                  (.pho parser + lower)
                                      │
                ┌─────────────────────┼─────────────────────┐
                │                     │                     │
            M2 photon.lib        M3 nanobind           M5 C++ frontend
            (algorithms)         (engine binding)      (Clang LibTooling)
                │                     │                     │
                │                     │                     │
                │                M4 Python frontend         │
                │                (@photon.kernel)           │
                │                     │                     │
                └─────────────────────┼─────────────────────┘
                                      │
                            M6 Convergence check
                       (Photon | Python | C++ → same Phonon IR)
```

## Critical rules (restated, Phase C emphasis)

- **R1 — Build bottom-up.** Phase B is done; Phonon is real and
  callable from C++. Phase A and Phase B tests must remain green
  at every commit.
- **R2 — Optimization lives in Phonon.** Photon, the Python
  decorator, and the C++ ingester emit *unoptimized* Phonon IR
  and let Phonon's existing pipeline run. None of them adds a
  pass. If you find yourself writing a circuit-shrinking pass in
  `photon/` — **stop**.
- **R3 — One C++ engine, one source of truth.** The `.pho`
  parser, the lib-call expander, and the Clang ingester are C++.
  Python appears in exactly two places: the `@photon.kernel`
  decorator (Python source-walking is best done in Python) and
  the thin nanobind binding that exposes the engine. There is no
  parallel compiler in Python.
- **R4 — Re-verify and pin.** Versions re-verified
  2026-06-16 (see [`../versions.md`](../versions.md) Phonon and
  Photon blocks). Highlights for Phase C:
  - LLVM/Clang/MLIR **22.1.8** (released 2026-06-16, final
    22.1.x).
  - nanobind **2.12.0** (BSD-3-Clause; released 2025-02-25;
    Phase C floor was ≥ 2.4 in the deep-dive).
  - CPython **3.13** (3.12 minimum: deep-dive's `3.12+` ask;
    `ast.parse(..., optimize=...)` and stricter constructors are
    in 3.13).
- **R5 — No vendor optimizer in our path.** Frontends only
  produce Phonon. Phonon and Spinor own everything below.

## Milestone scope summary

| M | What lands | Tests |
| --- | --- | --- |
| 1 | Photon language: `.pho` lexer + recursive-descent parser; OO model (`QReg`, gate methods, `measure_int`); lower to Phonon. | round-trip, golden Phonon-from-Photon, Grover kernel e2e via Phase B & A. |
| 2 | `photon.lib`: `bell_pair`, `ghz`, `qft`, `iqft`, `grover` (with oracle interface), `teleport`, `vqe_ansatz`. Each is a callable on `QReg`. | per-routine: simulator equivalence; Grover marginal; QFT vs FFT on small inputs. |
| 3 | nanobind binding `photon._engine`: `compile(phn_text, target_yaml) → CompiledProgram`; `CompiledProgram.estimate()`, `submit(...)`. | round-trip from Python: Phonon text in, compiled Spinor out; estimate fields populated. |
| 4 | `@photon.kernel`: decorator, AST visitor, supported-construct map (`QReg`, gate methods, `for`, `if` on bit, `measure*`). Rejection messages for arbitrary Python. | the 8-line Bell decorator example runs (sim) and prints `~50/50` histogram; rejection corpus golden. |
| 5 | C++ frontend: `photonc-cxx` driving Clang `FrontendAction` + `RecursiveASTVisitor`; YAML build config (`sources`, `entry`, `target`, `shots`). | a marked C++ Bell kernel compiles to Phonon and runs through Phase B+A. |
| 6 | Convergence check: same logical program (Bell, then GHZ, then a small Grover) in three forms produces equivalent compiled Spinor (matrix-equivalence on simulator). | three-way IR diff; behavioural histograms within 2σ. |

## Where to put new code

| You're adding… | Folder |
| --- | --- |
| A `.pho` keyword or grammar rule | `photon/lang/` |
| A `photon.lib` algorithm | `photon/lang/lib/` (C++) plus a Python facade |
| A nanobind type or function | `photon/bindings/` |
| A Python AST handler | `photon/frontends/python/photon/_decorator/` |
| A Clang AST visitor or matcher | `photon/frontends/cpp/lib/` |
| A test | `photon/tests/<m1..m6>/` |
| Anything in Spinor or Phonon | **Stop.** Wrong phase. |
