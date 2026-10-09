# Independent numerical audit

`audit.py` compares complete operators, including scalar phase and the initial
and final qubit mappings. Expected matrices use pinned Qiskit gate definitions
and independent NumPy Pauli/projector formulas. No SDK transpiler, compiler
matrix helper, or compiler simulator supplies an expected operator.

Install the test-only dependencies from `requirements.txt`, build `spinorc` and
`spinor_kak_audit_probe`, then run from any working directory:

```text
python audit.py --mode all --spinorc /path/to/spinorc --kak-probe /path/to/spinor_kak_audit_probe --output audit-evidence.json
```

On Windows, the executable names end in `.exe`. If required, pass
`--runtime-dir` with the directory containing the native runtime DLLs. The
`QSTACK_SPINORC` and `QSTACK_KAK_AUDIT_PROBE` environment variables also select
executables. Paths with spaces are accepted as individual quoted arguments.

`--mode cli` runs the public compilation matrix; `--mode kak` tests arbitrary
two-qubit matrices through the public synthesis API. `--mode large` exercises
large represented binary64 input angles. `--bases` restricts the target bases.
Fixtures live in a temporary directory. A failed comparison exits nonzero.

The deterministic seed is `0xB20261009`. Evidence includes source revision,
worktree diff hash, binary hashes, dependency versions, full-phase entry error,
spectral residual, resource counts, and routing-ancilla leakage. The default
short-circuit threshold is `2e-13`; this is numerical regression sensitivity,
not a certified whole-program bound. Finite samples do not prove symbolic
exactness, global optimality, or hardware fidelity.

`--seed`, `--haar-count` and `--random-count` control the deterministic corpus.
`--suite pr --haar-count 32 --mode all` runs 1,690 comparisons over all ten
bases, keeping O0–O3, reversed operands, local rotations and boundary fixtures.
The normal full run contains 6,490 comparisons. The nightly workflow uses three
fixed seeds, 1,024 Haar matrices per basis and 64 random source circuits, for
16,090 comparisons per seed. These are finite regression corpora, not samples
from a claimed representative hardware workload.

`high_precision.py` adds 248 independent full-operator comparisons through the
public `qstack.compile_file` API. It uses mpmath at 350 decimal digits, covering
represented angles up to `+/-1e300`, a subsequent small rotation, SU(2) scalar
phase at `+/-2*pi`, and retained interactions of `1e-8`, `1e-10` and `1e-12`.
It preserves the earlier audit's thresholds: `2e-10` for the 188 huge-angle
comparisons and `2e-13` for the 60 tiny-interaction comparisons. Expected gates
are independent Pauli/projector formulas. No provider account or submission is
used; the target fixtures and state directory are temporary.

```text
python high_precision.py --spinorc /path/to/spinorc --output high-precision.json
```

Both scripts detect single-configuration and MSVC `Release` build directories.
Evidence takes source and executable snapshots before and after the run and
records whether either changed. A dirty or changed snapshot is explicit; these
hashes alone do not assert that a binary was built from a particular source.
CI records the configure/build step immediately before execution. Package
installation and Windows/macOS/Linux compiler checks remain separate CI steps.

The existing `spinor_numerical_test` and `spinor_optimization_evidence_test`
provide dependency-free C++ regressions, including tiny interactions and
intentional discrepancies in numerical-report observations.

`physical.py` preserves the earlier independent physical audit in a portable
suite: 104 routing/MOVE comparisons, 12 sparse/directed/disabled-component cases,
4 Phonon instrument cases, 6 branch-join cases, and 62 offline compilations of
all 31 historical profiles at O0/O3. The default full run is 188 comparisons.
`--suite pr` uses three instead of eight random source circuits, retaining every
fixed adversarial case and all profiles: 128 comparisons. Each random source
uses the recorded seed 95321; `--seed` and `--random-count` are configurable.

```text
python physical.py --suite pr --spinorc /path/to/spinorc --phononc /path/to/phononc --output physical-pr.json
python physical.py --suite full --output physical-full.json
```

The compiler paths can also use `QSTACK_SPINORC`/`QSTACK_PHONONC`, and
`--runtime-dir` supplies Windows DLL locations. The default path resolver
supports single-configuration and MSVC Release builds. `--mode` selects routing,
availability, instrument, joins or profiles separately. Phonon is required for
the instrument group; profile serialization also requires the package's `qir`
extra. Use the PR command after the normal compiler build as a bounded CI step;
save its JSON with the other scientific evidence. It has no network or provider
submission operation. Working directories, discovery snapshots and source
programs are temporary. Its checked-in Phonon fixture is in `fixtures/`.

The physical oracle uses independently defined Pauli/projector matrices and
small complete operators; additional profile-native matrices come from the
separate pinned-SDK oracle in `audit.py`. It does not import the production
verifier, compiler gate helpers or simulator. Measurement/reset fixtures compare
Choi operators for every final classical outcome, including hidden reset
outcomes and arbitrary reference-entangled inputs. Unitary cases additionally
compare scalar phase without alignment. The unchanged threshold is `2e-9`.
MOVE checks explicitly reject population in its undefined doubly-occupied
subspace and vary the unknown phase consistently on each recorded locus.
Unused physical dimensions may be removed only after retaining all instruction,
initial and final mapping indices. The suite is bounded to eight active physical
qubits and does not claim exhaustive arbitrary-size, pulse or hardware validity.

The KAK corpus contains 256 Haar matrices and 87 fixed, phase-shifted, local,
and near-degenerate matrices per basis. The probe also reports the chosen
construction and any rejected analytical trial. A generic fallback is a valid
result when its complete operator passes; it must not be counted as evidence
that a shorter construction worked. Current iSWAP, sqrt-iSWAP and SYC candidates
must reduce the lexicographic `(two-qubit gates, all gates)` cost without
increasing the total number of gates, otherwise the validated incumbent stays.

The compiler's optional numerical report is a separate internal diagnostic.
It measures local complete-phase residuals for accepted rewrites, records
rejected trials separately, and always marks `certified: false`. An absent
numeric reconstruction guard is reported as `null`; it is not a zero residual
or a claimed threshold. Each unitary region retains its sum and maximum of
accepted local residuals, including in a program with measurement, reset,
classical operations or branches. These are unweighted observation summaries;
they cover neither all rounding, permutation and serialization steps nor a
dynamic execution path. Whole-program error remains unknown.

Algorithm references for the independently implemented candidates:

- [Huang et al., Quantum Instruction Set Design for Performance](https://arxiv.org/abs/2105.06074): constructive two-/three-SQiSW regions.
- [Zhang et al., Minimum construction of two-qubit quantum operations](https://arxiv.org/abs/quant-ph/0312193): two B interactions.
- [Cirq 1.7.0 analytical FSim construction](https://github.com/quantumlib/Cirq/blob/v1.7.0/cirq-core/cirq/transformers/analytical_decompositions/two_qubit_to_fsim.py): two FSim gates per B interaction, with the fixed SYC angle in the supported domain.
- [Cirq 1.7.0 SYC Ising construction](https://github.com/quantumlib/Cirq/blob/v1.7.0/cirq-google/cirq_google/transformers/analytical_decompositions/two_qubit_to_sycamore.py): controlled Ising interactions with two SYC gates.

The iSWAP candidate uses an owned Clifford-conjugation identity, documented in
`TwoQubitDecomposer.cpp`. Every candidate is reconstructed against the complete
target matrix before cost comparison. Analytical upper bounds describe the
ideal constructions; guarded numerical fallbacks may use more gates.

## Measured follow-up benchmark

The baseline is the clean audited revision `38791d60631cd87efb00eb28b5eb334a16d7b499`
([PR #22](https://github.com/nimesh08/quantum-stack/pull/22)). These paired figures
use the identical first 128 Haar matrices from seed `0xB20261009 + 1`, NumPy
2.5.3 and Qiskit 2.5.2. They compare native synthesis with the same native basis
and record arithmetic means. Baseline raw audit SHA-256:
`7ff5ebcaded85c0b3e719ced8d011568ec8d33831db0b1c006150f10f1004f6e`.

| Basis | Native gates, before → after | Two-qubit gates, before → after | Dependency depth, before → after |
|---|---:|---:|---:|
| iSWAP with RX/RZ | 48 → 39 | 6 → 3 | 33 → 23 |
| sqrt-iSWAP with PhasedXZ | 20 → 8.680 | 6 → 2.227 | 13 → 5.453 |
| inverse sqrt-iSWAP with PhasedXZ | 20 → 8.680 | 6 → 2.227 | 13 → 5.453 |
| SYC with PhasedXZ | 56 → 14 | 18 → 4 | 37 → 9 |

The follow-up checkpoint passed all 6,490 comparisons; worst full-phase entry
error was `4.9408043101399076e-14` at the unchanged `2e-13` threshold. The probe
was SHA-256 `d2e1aff8d7ec7978cf08863a9103580dd4a7107e4047e51dba9f99615ecf430d`;
the compiler was `7f69dd9618e11947ef50aad497b3e72a9433cfea6d26dff0297c9414fe44f7e8`.
This is checkpoint evidence on the follow-up implementation, not a release or
final-revision attestation; later CI artifacts identify their own source/build.

Across each changed basis's 343-matrix corpus (256 Haar plus 87 boundary/local
cases), 16 cases took the local shortcut. Analytical candidates were selected
for 313 iSWAP, 295 sqrt-iSWAP, 295 inverse sqrt-iSWAP and 311 SYC cases.
The retained generic candidates were rejected as non-improving in 14, 23, 23
and 2 cases respectively. Strict reconstruction/domain rejection occurred in
0, 9, 9 and 14 cases respectively. Those latter near-degenerate cases retain
four sqrt-iSWAP or twelve SYC gates and still pass complete-operator checks.
No Haar case in this corpus required a numerical fallback. The shorter
candidate is never accepted by relaxing the reconstruction threshold.

For the fixed two-qubit `random_0` source fixture in the SYC/PhasedXZ basis:

| Level | Native gates | Two-qubit gates | Dependency depth |
|---|---:|---:|---:|
| O0 | 178 | 90 | 148 |
| O1 | 121 | 90 | 106 |
| O2 | 14 | 4 | 9 |
| O3 | 14 | 4 | 9 |

These are all-to-all fixture metrics. Duration is unknown without calibration;
neither this table nor the Haar benchmark establishes global optimality or a
hardware speedup. The scientific script's `synthesis` records expose candidate
selection and rejection, so future comparisons need not infer it from counts.

## Coverage limits

The matrix suite covers unitary numerical behavior; it does not establish
dynamic program semantics. The separate semantic-instrument tests compare
measurement branches and quantum channels, while provider contract tests and
SDK object tests cover serialization. Report regions are separated at each
measurement, reset, classical instruction, barrier and branch marker. Region
sums/maxima include accepted observations only. A region without a measured
observation keeps null summaries, while a measured zero is retained. Rejected
optimization candidates contribute separate trial counters, never accepted
region totals.
Placement permutations, final scalar accumulation, compiler-to-text rounding
and remote provider processing are not all observed by the local report.
The report therefore retains `whole_program_error: null` and incomplete
coverage even when every listed local observation is measured. Rigorous
outward-rounded certificates and exhaustive arbitrary-size proofs remain
outside this implementation.
