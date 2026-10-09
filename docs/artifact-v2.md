# Artifact v2 and offline verification

The follow-up starts at audited revision `38791d6`. New compilations explicitly
write artifact and physical-IR version 2. Loading a v1 directory preserves its
schema and original hash preimage; it does not infer new classical semantics or
upgrade it in place. Recompile the original source when a v2 feature is needed.
The legacy direct Python `CompiledArtifact(...)` constructor defaults to v1 for
compatibility; new producers must explicitly select `schema_version=2`.

## Independent version contracts

| Contract | Current version | Compatibility |
|---|---:|---|
| Compiled artifact | 2 | v1 loads with its original hash calculation |
| Physical IR | 2 | v1 gate/readout semantics remain readable |
| Target snapshot | 2 | Earlier snapshots remain distinguishable |
| Submission options | 1 | Unchanged transport contract |
| Job receipt | 1 | Existing saved provider jobs remain retrievable |
| Execution result | 1 | Count widths and bit order are unchanged |
| Numerical report | 1 | Missing historical evidence is unavailable |
| Verification record | 1 | Bound to an artifact and oracle tool versions |

An artifact directory contains `manifest.json`, `logical.json`, `physical.json`,
`target.json`, `numerical.json`, `requirements.json`, `classical.json`,
`optimization.json`, `mappings.json`, `native.spinor` and the native program.
The manifest hashes the files. Its content identity additionally binds the
logical IR, numerical evidence, feature requirements and recorded limits.
It also binds the exact source-byte SHA256 and the executed compiler binary
SHA256 hashes. Compilation rejects binaries that change during the operation.
Verification records are separate files in `verification/`; adding a record
does not change the compiled program's identity.

## Exact classical state

`classical_values` defines immutable typed values. `classical_storage` defines
mutable bit locations and initialization. `classical_outputs` selects named
exported values; `exported_clbits` pins the previous public readout interface.
Unsigned values have widths 1–64. Storage bit lists are least-significant first;
serialized count strings keep the existing `c[n-1]...c[0]` ordering. Wide integer
literals use canonical decimal strings, never a floating-point JSON value.

Private storage can be reused after its last control-flow use. Existing exported
slots and typed outputs remain pinned. Measurements retain their quantum effect
even when their classical output is unused. A measured-bit snapshot is a copy
of an immutable result, not another measurement of the qubit.

Every required classical operation is checked against both the concrete device
and serializer capabilities. `supported`, `unsupported` and `unknown` are
distinct. Legacy `feedforward: full` does not imply integer arithmetic, loops or
new exported outputs. Historical profiles remain useful offline targets; they
do not constitute live validation of newly added controller features.

## Numerical evidence

`numerical.json` records accepted local transformations, regions, semantic input
and output digests, complete-phase matrix residuals, evaluation precision,
reconstruction thresholds, and rejected-candidate/fallback counts. Accepted
observations from discarded O3 candidates do not enter the final totals.
Parameter normalization, required decomposition, optimization and final native
canonicalization are represented, including explicit coverage gaps where no
local reconstruction was measured. A missing guard or measurement is `null`.

The report is a numerical diagnostic, not a certified error bound. The
intentional approximation budget remains zero independently of observed
floating-point residuals. Whole-program error is unknown. Region maxima and
sums cover the listed observations only; they do not certify serialization,
all rounding operations, or dynamic execution. A v1 artifact with no report
says evidence unavailable; it never receives an invented zero error.

Measured region sums and maxima remain available when the program contains
measurement, reset, classical operations or conditional branches. Each such
instruction, and each barrier, separates the numbered unitary regions; branch
entry, alternative and exit markers are separate boundaries. The sums include
only accepted rewrites assigned to that region across passes. They are neither
path-weighted nor combined into a whole-program estimate. Regions with no
measured rewrite keep null sums/maxima; measured zero remains zero. Rejected
layout or resynthesis trials contribute only to separate trial statistics.

## Offline verification

Install the optional verification dependencies:

```text
python -m pip install "heisenberg-spinor-submit[verify]"
qstack verify build/bell --no-env-file --json
qstack verify build/bell --verify-max-qubits 4 --verify-max-paths 256
```

This command reads the artifact and performs no authentication, discovery or
submission. It independently compares logical versus physical IR, physical
IR versus the stored native program, and physical IR versus the actual local
submission object constructed for this artifact. Expected operators do not use compiler
matrix/simulator helpers. It interprets OpenQASM 3/custom gate bodies, Braket
verbatim, supported Quantinuum QASM2, QIR control flow, Quil and native JSON.
The `physical_to_submission` check calls the same pure builder as live
submission, then interprets its output independently. It never creates a client,
loads credentials, invokes a provider transpiler or loads laboratory Python
configuration. Regression tests mutate the actual builder outputs (angle,
operand and readout) while keeping the stored IR/program unchanged.

| Route | Actual value checked for each artifact | Remaining boundary |
|---|---|---|
| IBM | Qiskit `QuantumCircuit`: scoped phase, ordered local matrices, branches, reset and readout | Supported one-bit branches; unsupported SDK classical expressions return `not_checked` |
| Google | Cirq circuit and an additional `submission_to_engine_protobuf` serializer/deserializer comparison | The pinned Engine default serializer uses float32 arguments. The user's threshold is never relaxed; a strict threshold may fail this distinct serialization check |
| IQM | Actual `Circuit` / `CircuitOperation` names, ordered loci, radians and readout keys | MOVE occupation/ideal semantics only; calibration-dependent phases are not certified |
| AWS | Actual Braket `Program.source`, interpreted as OpenQASM | Service validation and execution |
| OQC | Actual `QPUTask.program`, including decoded base64 QIR bytes | QAT processing and account capabilities |
| AQT | Actual Arnica REST submission body, with independent R/RZ/RXX half-turn conversion | No circuit SDK is used by this adapter. Partial retained quantum outputs with implicit all-qubit measurement return `not_checked` |
| IonQ / Anyon | Actual REST request circuit values | Provider processing and results retrieval; IonQ partial retained quantum outputs with implicit all-qubit readout return `not_checked` |
| Quantinuum / Azure / Rigetti / Alice & Bob | The actual shared upload/input value (QIR bytes, QASM, Quil or native JSON) | Remote Nexus/QCS/Felis/target processing is outside offline verification |
| Qibolab | The actual native assembler plan, including concrete physical identifiers and classical readout destinations | `submission_to_calibrated_pulses` remains `not_checked`: an artifact calibration fingerprint cannot establish pulse-template unitaries or fidelity. No Qibo circuit is constructed by this adapter |

Install both `verify` and the route extra for SDK object checks, for example
`python -m pip install "heisenberg-spinor-submit[verify,ibm]"`. An absent optional
SDK adds an explicit `not_checked` reason naming the required extra. Already
available logical/program checks retain their individual results; missing SDKs
never turn those checks into fabricated failures or passes. Direct REST/byte
builders need no authentication SDK for their offline value comparison.

Unitary programs use complete operators with scalar phase. Measurement, reset
and branches use complete quantum instruments, retaining every classical
outcome and tracing reset environments. Formats that omit unobservable scalar
phase are compared as instruments and explicitly report that coverage limit.
Stored native IR and the actual submission value are separate evidence stages.
Their comparison does not certify provider processing, credentials or hardware.

Default limits are four logical qubits, at most two additional active ancillas,
256 measurement/reset trajectories and 100,000 interpreted instructions. A
pre-allocation guard also limits declared interfaces to 65,536 physical slots
and 4,096 classical bits; active-wire bounds apply before SDK construction.
Sparse device indices remain supported (for example, two active wires on a
127-slot device). A
256 MiB dense-array working-memory budget also limits the combined qubit/path
dimensions; raising one limit does not remove the others. An
unknown instruction, unsupported control operation or exceeded budget returns
`not_checked` with a reason; it is never silently skipped. Results contain the
artifact/program hashes, oracle and submission-builder source hashes, dependency
versions and limits.
`passed` means that the finite numerical comparisons performed within those
limits passed, not that the whole compiler or hardware is certified.

Exit statuses are 0 (`passed`), 1 (`failed`) and 3 (`not_checked`). A legacy v1
artifact can have its physical program checked, but its absent logical IR is
explicitly not checked. Unknown versions and invalid file hashes are errors.

## Execution results for bounded loops

The provider job status and application status are separate. Each bounded loop
exports a correlated per-shot exhaustion flag, including zero on paths where
it does not execute. Normalized results preserve the complete original counts,
raw provider response and shot population. Named `classical_counts` are derived
only from actual correlated bitstrings, never probabilities.

After saving the result, the CLI exits 4 if any shot exhausted a loop bound.
It exits 5 if required correlated flags are unavailable, with an explicit
`not_checked` application status. It does not postselect, retry shots or emulate
device loops on the host. Saved receipts carry the output mapping, so these
statuses remain meaningful after restarting the CLI.
