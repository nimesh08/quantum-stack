# M6 — Borrowed optimizer adapters

## 1. Goal & spec section

Implement the "borrowed" half of the Phonon optimizer behind
**owned C++ interfaces**. Implements **Phonon Deep-Dive Part 3
§§7.4, 7.5** and the swap-policy commitments from §8.

Two interfaces:

1. **`ISynthesizer`** — classical-logic synthesis (oracle →
   reversible quantum sequence). Default implementation is a
   **NullImpl** (returns input unchanged). When
   `SPINOR_HAVE_TWEEDLEDUM` is set, a `TweedledumSynthesizer`
   adapter is compiled and selected. Default in CI: NullImpl
   (so the pipeline is exercised without the C++17 dependency).

2. **`IZxSimplifier`** — ZX-calculus simplification. Default
   implementation is a **NullImpl**. A second adapter,
   `PyZXSubprocessSimplifier`, shells out to `python3 -m pyzx`
   with a JSON contract; CI uses a **cassette mode** so it
   passes without PyZX installed. The Rust port `quizx` is
   documented as a future replacement (Decision D12 below).

Decision D12 records the integration depth choice (interface
stubs + one real adapter + cassette).

## 2. Inputs / outputs

```cpp
namespace phonon::optimizer {

struct ISynthesizer {
  virtual ~ISynthesizer() = default;
  // Walk the module; rewrite each `phonon.call` whose name
  // starts with "oracle." into an inlined sequence of Spinor
  // gates. Anything else is left untouched.
  virtual Stats synthesize(dialect::Module& m) = 0;
  virtual std::string name() const = 0;
};

struct IZxSimplifier {
  virtual ~IZxSimplifier() = default;
  virtual Stats simplify(dialect::Module& m) = 0;
  virtual std::string name() const = 0;
};

// Factories.
std::unique_ptr<ISynthesizer> makeNullSynthesizer();
std::unique_ptr<ISynthesizer> makeTweedledumSynthesizer();
std::unique_ptr<IZxSimplifier> makeNullZxSimplifier();
std::unique_ptr<IZxSimplifier> makePyZXSimplifier();   // subprocess + cassette

}  // namespace phonon::optimizer
```

## 3. Deliverables

- `phonon/optimizer/include/phonon/optimizer/BorrowedPasses.h`
- `phonon/optimizer/lib/Synthesizer.cpp` — NullImpl + cond-compiled
  Tweedledum impl.
- `phonon/optimizer/lib/ZxSimplifier.cpp` — NullImpl + PyZX
  subprocess driver + cassette loader.
- `phonon/optimizer/cassettes/zx/<program>.json` — recorded
  PyZX I/O for CI.
- Tests under `phonon/tests/m6/`.

## 4. Data structures

The cassette format (JSON):

```json
{
  "input_hash": "<sha256 of input gate-list>",
  "output": [
    {"kind": "h", "operands": [0]},
    {"kind": "rz", "operands": [0], "angle": 1.5708}
  ]
}
```

Cassette location: `${PHONON_CASSETTE_DIR}/zx/<basename>.json`.
The PyZX simplifier first looks for a cassette; if found, plays
it back. If not, and `PHONON_PYZX_LIVE=1`, it shells out to
PyZX. Otherwise it emits a warning and falls back to NullImpl.

## 5. Algorithms

### TweedledumSynthesizer (compiled iff SPINOR_HAVE_TWEEDLEDUM)

For each `phonon.call` whose name has the prefix `oracle.`:

1. Parse the call's classical-arg expression list.
2. Hand the expression to `tweedledum::synth_pkrm` (or
   `tweedledum::stg_synth`) to get a circuit.
3. Replace the call op with the synthesised gate sequence.

### PyZXSubprocessSimplifier

For each maximal block of `spinor.*` gate ops between
barriers/measures:

1. Serialise the block to a deterministic JSON ("blocks.json").
2. Compute the SHA256 hash of the canonical JSON.
3. Look for a cassette `${PHONON_CASSETTE_DIR}/zx/<hash>.json`.
4. If found, replace the block with the cassette's recorded
   output. If not, and `PHONON_PYZX_LIVE=1` is set, shell out
   to a small Python helper `phonon/optimizer/pyzx_helper.py`
   which reads stdin JSON, runs `pyzx.simplify.full_reduce`,
   writes stdout JSON, and exit. If neither is available,
   warn and skip.

For Phase B's CI we ship cassettes for the corpus circuits:
`bell.json`, `ghz.json`, `qft_loop.json` — each is the
identity transformation (PyZX returns the same gates), so the
simplifier behaves like NullImpl on the corpus but the
plumbing is exercised end-to-end.

## 6. Test matrix

| Test | Type | Inputs | Asserts |
| --- | --- | --- | --- |
| `M6_synth.null_passthrough` | unit | bell module | NullImpl returns unchanged. |
| `M6_synth.factory_returns_named` | unit | factory | name == "null" or "tweedledum". |
| `M6_zx.null_passthrough` | unit | bell module | NullImpl returns unchanged. |
| `M6_zx.cassette_replay_identity` | integration | bell module + cassette | output matches recorded output. |
| `M6_zx.unknown_block_warns_when_no_cassette` | unit | random module + no cassette | a warning is emitted; module is unchanged. |
| `M6_swappable.same_pipeline_with_either_impl` | property | two pipeline runs | gate counts are identical. |

## 7. Cookbook example

```cpp
auto synth = phonon::optimizer::makeNullSynthesizer();
auto zx    = phonon::optimizer::makePyZXSimplifier();
auto stats = phonon::optimizer::runBuiltPipeline(m);
synth->synthesize(m);
zx->simplify(m);
// Pipeline order: built passes → synthesis → ZX simplification
// → built passes again (M7 wires this).
```

## 8. Pitfalls

- **Subprocess hardening.** The PyZX helper must not block on
  stdin/stdout if the user's environment lacks Python. Use
  `popen` with explicit timeout + non-zero exit code → fall
  back to NullImpl.
- **Cassette drift.** A cassette is keyed on the canonical
  input JSON's hash; a Phase B M5 pass that reorders ops can
  invalidate the hash. Cassettes are therefore recorded
  **after** the built pipeline runs.
- **Tweedledum's qubit numbering.** Tweedledum uses 0-based
  contiguous indexing; the adapter must remap to/from Spinor
  ValueIds.

## 9. Definition of Done

- [ ] All test matrix rows green under `ctest -L phonon_M6`.
- [ ] CI passes without `SPINOR_HAVE_TWEEDLEDUM`, without
      Python or PyZX installed.
- [ ] `phaseB_progress.md` entry appended; D12 recorded in
      `phaseB_decisions.md`.

## 10. Open questions

None.
