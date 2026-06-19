# Phase D — Glossary

A small list of terms that appear in the platform docs. The compiler-
side glossaries (`phaseA/glossary.md`, `phaseB/glossary.md`,
`phaseC/glossary.md`) cover the quantum and IR vocabulary; this one
covers the platform-side jargon.

| Term | Meaning |
|------|---------|
| **Job** | A persistent record of a submission. Lives in the `jobs` table; passes through Submitted → Queued → Running → Completed (or Rejected / Failed). |
| **Cost control seam** | The pre-Queued check that compares the compiler's `ResourceEstimate` (multiplied by `chip.pricing.per_shot_usd`) to the user's `Budget`. Over-budget → 402 + `state=Rejected` before any spend. |
| **Calibration refresh seam** | The nightly APScheduler job that fetches each chip's calibration JSON and writes it to `~/.cache/spinor/calibration/<chip>.json` (the path the chip YAML already declares). The compiler picks up the new file on its next compile. |
| **Verbatim submission** | The Phase A adapter contract: jobs go to providers in pass-through mode (`skip_transpilation=True` for IBM, `Program(source=qasm_text)` for Braket, raw `openqasm-v3` for Azure). Rule 5. |
| **Spinor source / Phonon source / Photon source** | The three accepted kinds for `POST /jobs`. Phonon is the engine's native input; Spinor is wrapped as a trivial Phonon program; Photon is lowered via the C++ frontend. |
| **Estimate** | `ResourceEstimate { num_qubits, depth, two_qubit_count, t_count }` — what the compiler returns and what cost control consults. |
| **Histogram** | The result the worker stores in `results.counts` (keys are bitstrings, values are shot counts). |
| **Lease** | The `claim_expires_at` window during which a worker holds a Running job. If the worker dies, another reclaims after the lease expires. |
| **API key** | `prefix.body` plaintext returned once on creation; bcrypt-hashed thereafter; identified by the 8-character prefix. |
| **Cassette mode** | `SPINOR_SUBMIT_MODE=cassette` — the Phase A adapter replays recorded JSON instead of hitting a provider. Used in CI and dev. |
