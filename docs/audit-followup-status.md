# Audit follow-up implementation

Baseline: `38791d60631cd87efb00eb28b5eb334a16d7b499` (audited PR #22).
Follow-up branch: `feat/audit-language-expansion`. The baseline branch and PR are
preserved. The follow-up is reviewed against that exact baseline. This page
separates implementation and offline evidence from account or hardware readiness.

| Milestone | Work | Acceptance evidence |
|---|---|---|
| 1 | Artifact/IR v2, exact integers, typed classical contracts, logical API names | Implemented. Frozen v1 hash/receipt tests, exact integers above 2^53, format/device contracts and installed-wheel compatibility checks. |
| 2 | Accepted-transformation numerical diagnostics and independent offline verification | Implemented. Complete operators/instruments, serializer mutation tests, portable seeded and high-precision audits. |
| 3 | Saved values, fixed-width integers, bounded adaptive loops, static conditional allocation | Implemented for the documented finite language subset. Phonon/Photon/Python regressions, independent instrument checks and real local execution. |
| 4 | Pre-routing simplification, disjoint-wire blocks, analytical synthesis, private storage and joins | Implemented. Full-phase native synthesis corpus, tiny interactions, storage interference and branch-compensation tests. |
| 5 | Operation-specific heterogeneous placement/routing and bounded deterministic search | Implemented. Disjoint/directed loci, disabled resonators, readout-only sites, branch joins, deterministic budget exhaustion and all 31 profiles. |
| 6 | Calibration/resources, advisory scheduling, separate account/hardware evidence | Implemented. Ordered calibration/resource tests and manual harness contracts. Account and hardware validation have not been run. |

Review the contracts in [artifact v2](artifact-v2.md), the supported
[controller language](language/controller.md), the [Photon surface](photon-controller.md),
[routing](heterogeneous-placement.md), and the [manual validation harness](validation-harness.md).
The [scientific suite](../spinor/tests/scientific/README.md) records reproducible
seeds, thresholds, before/after native counts, depth and fallback rates. Final
revision evidence is separate from its clearly labeled development checkpoints.

New artifacts bind their original logical IR, physical IR, numerical diagnostics,
feature requirements and limits into their content hash. Version-1 artifact hashes
retain their original preimage. Receipts, results and target snapshots have
independent version constants. Old artifacts report numerical evidence unavailable.

`qstack verify ARTIFACT` performs offline finite operator/instrument comparisons.
It never submits jobs. Exit codes are 0 (passed), 1 (failed), and 3 (not checked).
Unknown instructions and exceeded coverage limits are explicit. A passed finite
numerical comparison is not a certified bound on whole-program error.

Loop exhaustion is an application status separate from the provider job status.
All shots and raw results are retained before CLI exit 4; missing correlated flags
produce an explicit unchecked application result and exit 5.

Account-access and hardware-executed evidence remain separate from offline tests.
No account or hardware jobs, merges, or releases are authorized by this work.

## Compatibility and limits

All 31 YAML profiles, 11 vendors and 12 cloud routes are preserved. Aqumen and
Falcon cloud remain unavailable; a configured Qibolab laboratory bridge is
separate. New controller features require explicit device and serializer
contracts. An explicit `unknown` capability cannot inherit legacy permission.

Python/Photon kernels support fixed-type conditional results. Phonon helpers
can declare Boolean/UInt and mixed quantum/classical results, including tuple
results, with fixed types and arity across returned paths. Legacy quantum in/out
helpers remain compatible. The Builder normalizes conditional returns and
in-body bounded-loop break/continue transfers, retaining explicit carried
values. Its static For bounds use exact signed integers, including bounds above
2^53. These compiler constructs do not execute arbitrary host Python functions.
Runtime angles, dynamic quantum indices/sizes, multiplication/division,
unbounded loops and a quantum heap remain unsupported with diagnostics.

## Follow-up acceptance checks

The completion work addresses the five gaps found in the literal coverage
review of `196b862`, plus regressions exposed by the new checks. The numerical
change is independently reviewable from `7d85c04`; later commits retain that
history and the original audited baseline.

| Requirement | Permanent regression coverage |
|---|---|
| Per-region numerical sums in measured/conditional programs, including correct fences and accepted O3 trials only | `spinor_optimization_evidence_test`, `test_numerical_regions.py` |
| Actual per-artifact submission-object verification | `test_artifact_submission_verification.py`, isolated SDK jobs, installed-package missing-SDK and real-IBM checks |
| Typed helper and Builder conditional returns | Phonon M3/M4 tests and `test_typed_helpers.py`, including complete reference-entangled instruments |
| Builder break/continue/return transfers and loop status | Phonon M4 tests, source/helper bounded-loop tests and existing Python controller tests |
| Exact legacy Builder For bounds | Phonon M4 cases above 2^53 and at signed 64-bit boundaries |

`qstack verify` keeps separate logical, stored-program and actual submission
checks. A missing SDK is `not_checked`, with an installation instruction; it does
not erase the available checks. Google protobuf float32 conversion is measured
under the caller's unchanged tolerance. Qibolab's native assembler plan is
checked, while calibrated pulse semantics remain explicitly `not_checked`
without pulse evidence. These coverage limits are never counted as successful
account access or hardware execution.

Search and branch-join enumeration are finite, and numerical fallbacks remain
legal when a shorter analytical candidate fails its unchanged strict checks.
Supplied schedules are advisory. Unknown duration, whole-program error, account
access or hardware evidence remains unknown; none is inferred from a passing
offline simulation or a provider name.
