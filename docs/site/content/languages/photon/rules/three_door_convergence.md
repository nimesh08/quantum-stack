# Three-door convergence

The compiler proves that the same logical kernel — written as `.pho`,
as `@photon.kernel`, or as `[[photon::kernel]]` — produces the
**same Spinor IR** and the **same `ResourceEstimate`**.

## Why this matters

It guarantees that the language you choose to write in is purely a
matter of taste. There's no penalty in fidelity or cost for picking
Python over the canonical `.pho`.

## How it's enforced

The convergence test
[`photon/tests/m6/convergence_test.cpp`](https://github.com/nimesh08/quantum-stack/blob/main/photon/tests/m6/convergence_test.cpp)
compiles a 6-file corpus (Bell + GHZ × 3 frontends each) and asserts:

```cpp
EXPECT_EQ(profile_pho, profile_python);
EXPECT_EQ(profile_pho, profile_cpp);
EXPECT_EQ(estimate_pho, estimate_python);
EXPECT_EQ(estimate_pho, estimate_cpp);
```

A "profile" is a deterministic summary of the compiled Spinor IR
(gate counts, structure, ordering). The estimate is the same
[`ResourceEstimate`](../../../reference/python/index.md) the API
returns.

## What can break it

- Adding a `lower` step that depends on Python-only state.
- Calling a `photon.lib` routine from one frontend that's not
  exposed in another.
- Frontend-specific normalisation (e.g. evaluating `pi/2` differently).

The convergence test catches these immediately.

## Run it

```bash
ctest --test-dir build -R photon_M6_convergence
```

## See also

[Bell, three doors](../cookbook/bell_three_doors.md), every frontend page
