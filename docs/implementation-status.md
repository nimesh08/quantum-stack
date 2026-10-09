# Implementation acceptance checklist

This checklist tracks the approved compiler/submission plan against audited
baseline `5125b64f`. Test execution evidence belongs to the PR and CI runs for
the exact commit being reviewed. A passing contract test is not evidence of
provider account access or hardware execution.

| Plan area | Implementation and verification |
|---|---|
| Source semantics | Photon/Phonon preserve indexed and stepped loops, bounded compile-time while loops, specialized angles/indices/allocation sizes/loop bounds, classical branches, measurement/reset, scalar phase and sparse readout. Frontend and lowerer regressions exercise both branches, lexical captures, repeated/nested calls, early returns and parameter shadowing. Unsupported runtime loops, recursion and unbound oracles fail explicitly. |
| Owned compilation | C++ owns exact native synthesis, local/commutation/block simplification, O0-O3 layout/routing searches and native legality. Native matrix, randomized equivalence, directed/disconnected topology and no-provider-transpiler tests cover the pipeline. Approximate synthesis is disabled with a zero error budget. |
| IQM resonators | Discovery retains actual computational/resonator slots and ordered MOVE/CZ loci. Owned routing emits balanced native sequences. Tests cover multiple resonators, sparse high physical indices, capacity, invalid occupation and independent MOVE phase gauges. |
| Registry | All 31 historical profiles across 11 vendors remain labelled with readiness and provenance. Static synthetic topology cannot authorize live submission. Account discovery resolves concrete devices; submission revalidates the native artifact against current capabilities. |
| Artifact/results | Versioned artifacts retain native program, physical IR, snapshot, optimization report, hashes and mappings. Versioned receipts persist for restart/retrieval. Counts accept actual integer histograms only; raw probability output remains separate. Local execution uses the C++ simulator. |
| Configuration | One typed field schema generates CLI flags, help, environment aliases, validation and credential classification. Tests cover source precedence, repeated dotenv paths/aliases, paths with spaces, interpolation disabled, secure-store opt-in and secret file/stdin handling. |
| Job handling | Explicit live/local/cassette modes, offline dry-run validation, authenticated read-only checks, bounded read retries, no blind submission retries, persistent receipts and functional cost-cap rejection. Successful responses and saved records redact echoed secrets as well as credential fields. |
| Twelve cloud routes | IBM, Google, Quantinuum, Azure, AWS, IonQ, Rigetti, IQM, OQC, AQT, Anyon and Alice & Bob have adapters and offline request/result contracts. Pinned optional SDK jobs verify real serialization/signatures with network blocked. IBM directly constructs native circuits for SamplerV2. |
| Optional Qibolab | Built-in assembly uses a configured platform's native calibrated pulses and actual acquisition samples. A custom bridge remains optional. Real SDK tests are offline; dummy controllers are rejected for live execution. |
| Scheduling | Reports use supplied durations and exclusive resources; measurements constrain conditional gates even on previously unused qubits. Missing durations remain unknown. Untimed provider formats do not claim enforced pulse timing. |
| Release preparation | Cross-platform compiler/provider CI, isolated package installation, release build workflows, provider setup examples and compatibility migration are included. Merging or publishing a release is a separate repository operation. |

## External requirements

- **Account-access verified:** requires credentials, subscriptions, concrete
  target access and an authenticated read-only check. No such verification is
  claimed by offline CI.
- **Hardware-executed:** requires a separately configured live smoke test and
  any provider approval. Hardware jobs are not part of the offline test run.
- **OQC:** its public calibration payload does not define a normalized native
  topology contract. Live compilation requires an authoritative account
  capability snapshot tied to the actual QPU and calibration digest.
- **Qibolab:** real execution requires calibrated platform files and instrument
  connections. The compiler cannot invent a laboratory's pulse calibration.
- **Aqumen/QCI and TII Falcon cloud:** unavailable until the missing vendor
  SDK/authentication/submission contracts are supplied, as agreed in the plan.

Detailed formats, processing boundaries and source references are in the
[provider contract reference](../spinor/submit/python/qstack/providers/CONTRACTS.md).
