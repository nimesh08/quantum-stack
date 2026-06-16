# M6 — Decomposition (Euler ZYZ + closed-form 2-qubit recipes)

## 1. Goal & spec section

The pass that rewrites every standard gate (`h`, `x`, `y`, `z`,
`s`, `t`, `rx`, `ry`, `rz`, `cx`, `cz`, `swap`, …) into an
equivalent sequence of the chip's native gates. Implements
[Spinor Engineering Deep-Dive][deep-dive] Part 2 §5 (the two
decomposition theorems) and §6 (the post-decomposition cleanup).

Every rewrite is *exact* — a mathematical identity, not an
approximation. The Solovay–Kitaev fallback (for chips with
discrete-only native sets) is held in reserve.

[deep-dive]: ../../spinor_engineering_deep_dive.docx

## 2. Inputs / outputs

- **Consumes:**
  - A routed `dialect::Module` (chip-target attribute set,
    every 2q gate on a wired pair).
  - The chip's `ChipInfo` (drives recipe selection).
- **Produces:**
  - A new `dialect::Module` with every gate in the chip's
    native set.
- **Invariants on output:**
  - Every gate's mnemonic is in `chip.nativeGates`.
  - The IR remains in SSA value-semantics shape (M1
    structural verifier passes).
  - No standard-gate ops remain (cx, h, s, t, rx, ry, rz, …
    have all been rewritten).

## 3. Deliverables

- `spinor/passes/include/spinor/passes/Decomposition.h`
- `spinor/passes/lib/Decomposition.cpp` — driver that walks
  the IR and applies recipes per op kind.
- `spinor/passes/lib/Recipes.h` + `Recipes.cpp` — closed-form
  gate-by-gate recipes per chip native-set family.
- `spinor/passes/lib/Cleanup.cpp` — post-decomposition local
  peephole.
- Header-only `spinor/passes/lib/Complex2x2.h` — tiny 2×2
  complex matrix helper for ZYZ; avoids the full Eigen
  dependency for Phase A. (Eigen FetchContent is documented
  as a Phase B follow-up since Phase A's matrix work is
  small and closed-form.)
- `spinor/tests/m6/CMakeLists.txt`
- `spinor/tests/m6/decomposition_test.cpp`
- `spinor/tests/m6/recipes_test.cpp`
- `spinor/tests/m6/cleanup_test.cpp`

## 4. Data structures

```c++
namespace spinor::passes {

class Decomposition {
 public:
  // Lower every standard gate in `m` to the chip's native set,
  // applying the registry-driven recipes. The output is a fresh
  // module; the input is unchanged.
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip,
                      dialect::Diagnostics& diag) const;
};

class Cleanup {
 public:
  // Local peephole: merge adjacent rotations (rz(a)·rz(b) =
  // rz(a+b)), annihilate inverse pairs (sx · sxdg = id), drop
  // redundant identity rotations.
  dialect::Module run(const dialect::Module& m) const;
};

}  // namespace spinor::passes
```

## 5. Algorithms

### Single-qubit Euler-ZYZ

For any 2×2 unitary `U = [[a,b],[c,d]]`, there exist α, β, γ
such that `U = e^{iφ} · Rz(γ) · Ry(β) · Rz(α)`. Closed-form
formulas (we use the convention `Rz(θ) = diag(e^{-iθ/2},
e^{iθ/2})`):

```
β = 2 · atan2(|b|, |a|)
α = arg(d) - arg(b)            if sin(β/2) != 0
γ = arg(d) + arg(b)            if sin(β/2) != 0
α + γ = 2 · arg(d)             if sin(β/2) == 0
```

For named gates we use exact precomputed angles (avoiding
arg/atan2 round-off):

| Standard gate | (α, β, γ) ZYZ |
| ------------- | -------------- |
| h    | (0, π/2, π) |
| x    | (0, π, 0)   |
| y    | (-π/2, π, π/2) |
| z    | (0, 0, π)   |
| s    | (0, 0, π/2) |
| sdg  | (0, 0, -π/2) |
| t    | (0, 0, π/4) |
| tdg  | (0, 0, -π/4) |
| rx(θ)| (-π/2, θ, π/2) |
| ry(θ)| (0, θ, 0)   |
| rz(θ)| (0, 0, θ)   |

Each ZYZ then expands into the chip's native one-qubit
vocabulary via per-chip recipes (see Recipes below).

### Per-chip recipes

Recipes are keyed on the chip's `decompose.two_qubit.entangler`
plus `decompose.one_qubit.{rotation_gate, pi_2_gate}`.

#### IBM-class (entangler=ecr, rotation=rz, pi_2=sx)

- `Rz(θ)` → `rz(θ)` (identity).
- `Ry(θ)` → `rz(-π/2)  sx  rz(θ)  sx  rz(π/2)`  (the standard
  identity `Ry = Rz(-π/2) · X^{1/2} · Rz · X^{1/2} · Rz(π/2)`).
- `H`     → `rz(π/2)  sx  rz(π/2)`. (Up to global phase.)
- `X`     → `sx  sx`  (X = √X · √X up to global phase, then a Z
  flip; we use `rz(π) sx sx` to be exact).
- `CX(c,t)` → `rz(π/2)_t  sx_t  ecr(c,t)  sx_t  rz(π/2)_c
                rz(-π/2)_t`. (Standard CX-from-ECR identity.)
- `CZ`    → `H` on target, then `CX`, then `H`.
- `SWAP`  → three CX (or, on chip, three ECRs with
  appropriate single-qubit rotations).

#### IonQ-class (entangler=ms, rotation=gpi, pi_2=gpi2)

- `Rz(θ)` is virtual on IonQ but for simplicity we emit
  `gpi(θ)` (parameterised single-qubit phase). The exact
  sequence is documented inline.
- `CX` → `gpi2(π/2)_c  ms(c, t)  gpi2(-π/2)_c  gpi2(-π/2)_t`
  (a known closed-form IBM-style identity adapted to MS).
- `H` → `gpi2(π/2)  gpi(0)`.
- `Rx(θ)` → `gpi2(0) → gpi(θ/2) … ` (per IonQ docs).

The exact angle constants are listed in `Recipes.cpp`. Where
identities are up to global phase, the test suite checks
matrix equivalence up to a complex unit-modulus factor.

#### Quantinuum-class (entangler=rzz, rotation=u1q, pi_2=null)

- `CX` → uses RZZ + U1q wrappers per Quantinuum's reference
  decomposition (also explicit in `Recipes.cpp`).

### Cleanup

After decomposition we may have:

- Two adjacent `rz`s on the same physical qubit → merge:
  `rz(a) rz(b)` → `rz(a + b)`.
- A pair `sx sxdg` (or `sxdg sx`) → annihilate.
- An `rz(0)` or `rz(±2π·k)` → drop.

Implementation: linear sweep over the op list with a
small look-back. Idempotent.

### Equivalence (used by tests)

For 1q gates: build the 2×2 unitary of the original op and the
2×2 unitary of the emitted native sequence, check they are
equal up to a complex unit-modulus factor (`|det(A·B*)| =
1` and `A · e^{-iφ} = B`).

For 2q gates: same idea on 4×4 unitaries. The CX/CZ/SWAP
cases use known closed forms; we don't need general SVD.

## 6. Test matrix

| ID    | Name                               | Type        | Inputs                                | Expected output                                              | CI gate        |
| ----- | ---------------------------------- | ----------- | ------------------------------------- | ------------------------------------------------------------ | -------------- |
| M6-T01 | `M6_zyz.standard_gates_round_trip` | property    | each named 1q gate                    | unitary equality up to global phase, ≤1e-10                 | `ctest -L M6`  |
| M6-T02 | `M6_zyz.random_rotations`         | property    | random angles for rx/ry/rz           | 100 trials all unitarily equivalent                         | `ctest -L M6`  |
| M6-T03 | `M6_recipes.cx_ibm`                | unit        | cx via ecr/sx/rz                      | 4×4 unitary matches CX up to global phase                   | `ctest -L M6`  |
| M6-T04 | `M6_recipes.cx_ionq`               | unit        | cx via ms/gpi/gpi2                    | matrix equivalence holds                                    | `ctest -L M6`  |
| M6-T05 | `M6_recipes.cx_quantinuum`         | unit        | cx via rzz/u1q                        | matrix equivalence holds                                    | `ctest -L M6`  |
| M6-T06 | `M6_decompose.bell_ibm`            | integration | Bell IR after M5 routing on IBM       | every output op is in IBM native set                        | `ctest -L M6`  |
| M6-T07 | `M6_decompose.bell_ionq`           | integration | Bell IR on IonQ                       | every output op is in IonQ native set                       | `ctest -L M6`  |
| M6-T08 | `M6_decompose.no_standard_gates_remain` | property | corpus across 4 chips                 | post-decompose IR has zero ops with `isStandardGate(kind) && kind ∉ {AllocQubit, AllocBit, Measure, Reset, Barrier}` | `ctest -L M6` |
| M6-T09 | `M6_cleanup.merges_rz_rz`          | unit        | rz(0.3) rz(0.4)                       | single rz(0.7)                                              | `ctest -L M6`  |
| M6-T10 | `M6_cleanup.annihilates_inverse`   | unit        | sx sxdg                                | empty (op annihilated)                                      | `ctest -L M6`  |
| M6-T11 | `M6_cleanup.idempotent`            | property    | run cleanup twice                     | second run is no-op                                         | `ctest -L M6`  |
| M6-T12 | `M6_decompose.fourth_chip_path`    | integration | bell on `ionq_aria_proto`             | decomposes via the same recipes as `ionq_forte`             | `ctest -L M6`  |

## 7. Cookbook example — Bell on IBM Heron r2

After M5 routing the IR for Bell looks like:

```
spinor.module @main attributes {target = "ibm_heron_r2"} {
  %q0 = spinor.alloc_qubit
  %q1 = spinor.alloc_qubit
  ; 154 more allocs (the chip's other physical qubits)
  ...
  %q0_1 = spinor.h %q0
  %q0_2, %q1_1 = spinor.cx %q0_1, %q1
  %c0_1 = spinor.measure %q0_2
  %c1_1 = spinor.measure %q1_1
}
```

After M6 decomposition (only the Bell-relevant lines shown):

```
%a = spinor.rz %q0   {angle = 1.5707963267948966}   ; π/2
%b = spinor.sx %a
%c = spinor.rz %b    {angle = 1.5707963267948966}   ; the H expansion
%d = spinor.rz %c    {angle = 1.5707963267948966}
%e = spinor.sx %d
%f, %g = spinor.ecr %e, %q1
%h = spinor.sx %f
%i = spinor.rz %h    {angle = 1.5707963267948966}
%j = spinor.rz %g    {angle = -1.5707963267948966}
; (with cleanup, adjacent rz(π/2) rz(π/2) = rz(π) gets folded)
```

## 8. Pitfalls

- **Global phase.** The recipes are correct up to global
  phase; the tests must compare up to a unit-modulus factor.
- **Numerical tolerance.** Closed-form recipes can still
  accumulate float error when chained. Tests use 1e-10
  tolerance on matrix coefficients.
- **Op ordering for 2q gates.** Recipes that emit
  single-qubit gates around an entangler must thread the
  output values correctly (one operand becomes the
  surrounding-rotations target, the other the entangler-only
  side).
- **Recipe-set growth.** A new chip with a new entangler
  needs a new recipe entry — but no new code path. Recipes
  are looked up from a small static table keyed on
  `(rotation_gate, pi_2_gate, entangler)`.
- **Cleanup reordering.** Cleanup is allowed to merge
  adjacent same-qubit rotations but MUST NOT cross a
  measure/reset/barrier (those are explicit fences).

## 9. Definition of Done

- [ ] Spec landed.
- [ ] All M6-T## tests pass.
- [ ] Post-decompose IR for the corpus has zero standard-gate
      ops on each of the four chips.
- [ ] Cleanup is idempotent.
- [ ] Test matrix updated.
- [ ] Progress journal appended.
- [ ] Decisions log: D6 (closed-form recipes vs general
      Eigen-based KAK; Eigen FetchContent deferred to Phase B
      since Phase A's matrix needs are small).

## 10. Open questions

None. The recipe set covers all four chips in the registry.
