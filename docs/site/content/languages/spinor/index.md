# Spinor

Quantum assembly — one line per machine operation, no loops, no
functions, no expressions. The lowest layer a human still writes.

## On this page

- [Overview](overview.md) — the two contracts (`target generic` vs chip)
- [Lexical structure](lexical.md) — tokens, comments, identifiers, literals
- [Grammar](grammar.md) — the 22-rule EBNF, walked through
- [Types](types.md) — `qubit` and `bit`

## Reference (every keyword has its own page)

| Category | Pages |
|---|---|
| Declarations | [`target`](reference/target.md) · [`qubit`](reference/qubit_decl.md) · [`bit`](reference/bit_decl.md) |
| One-qubit Cliffords | [`h`](reference/h.md) · [`x`](reference/x.md) · [`y`](reference/y.md) · [`z`](reference/z.md) · [`s`](reference/s.md) · [`sdg`](reference/sdg.md) |
| One-qubit non-Cliffords | [`t`](reference/t.md) · [`tdg`](reference/tdg.md) |
| Continuous rotations | [`rx`](reference/rx.md) · [`ry`](reference/ry.md) · [`rz`](reference/rz.md) |
| Two-qubit standard | [`cx`](reference/cx.md) · [`cz`](reference/cz.md) · [`swap`](reference/swap.md) |
| Native two-qubit | [`ecr`](reference/ecr.md) · [`ms`](reference/ms.md) · [`rzz`](reference/rzz.md) |
| Native one-qubit | [`sx`](reference/sx.md) · [`sxdg`](reference/sxdg.md) · [`gpi`](reference/gpi.md) · [`gpi2`](reference/gpi2.md) · [`u1q`](reference/u1q.md) |
| Measurement / housekeeping | [`measure`](reference/measure.md) · [`reset`](reference/reset.md) · [`barrier`](reference/barrier.md) |

## Legality rules

| W | Rule |
|---|---|
| [W1](rules/W1_declare_before_use.md) | declare before use |
| [W2](rules/W2_index_in_range.md) | indices in range |
| [W3](rules/W3_distinct_operands.md) | distinct operands on multi-qubit gates (no-cloning, syntactically) |
| [W4](rules/W4_no_op_after_measure.md) | no gate on a measured qubit (chips without `mid_circuit_measure`) |
| [W5](rules/W5_portable_contract.md) | the portable contract — `target generic` |
| [W6](rules/W6_hardware_contract.md) | the hardware contract — chip-locked |

## Cookbook

Recipes mixing the keywords:

- [Bell program](cookbook/bell.md) — the smallest entangler
- [GHZ state](cookbook/ghz.md) — n-qubit entanglement
- [Arbitrary rotation](cookbook/arbitrary_rotation.md) — `rx/ry/rz` combinations
- [Parameterised gates](cookbook/parameterized_gates.md) — `pi`, `pi/2`, `2*pi/3`
- [Reset and reuse](cookbook/reset_and_reuse.md) — measure → reset → re-use
- [`barrier` for the optimizer](cookbook/barrier_for_optimizer.md)
- [IBM native gate set](cookbook/ibm_native_gates.md) — ECR + RZ + SX
- [IonQ native gate set](cookbook/ionq_native_gates.md) — MS + RZZ + GPI/GPI2
- [Routing via `swap`](cookbook/routing_via_swap.md)
- [All-to-all vs heavy-hex](cookbook/all_to_all_vs_heavy_hex.md) — same program, two chips

## Where to put new code

| Adding… | Folder |
|---|---|
| A new gate mnemonic | `spinor/parser/cpp/lib/Lexer.cpp` (the gate set) + a passes change |
| A new chip | `spinor/registry/chips/<id>.yaml` — **no compiler change** |
| A new emitter | `spinor/emit/` |
