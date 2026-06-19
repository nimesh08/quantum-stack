# Glossary

Terms used across the platform docs. For quantum and IR vocabulary
(qubit, gate, decomposition, KAK, etc.) see the
[Spinor](../languages/spinor/index.md) and
[Phonon](../languages/phonon/index.md) language references.

| Term | Meaning |
|---|---|
| **Job** | Persistent record of a submission. Lives in the `jobs` table; passes through Submitted → Queued → Running → Completed (or Rejected / Failed). |
| **Cost-control seam** | Pre-Queued check comparing `ResourceEstimate × chip.pricing.per_shot_usd` to the user's `Budget`. Over-budget → 402, `state=Rejected`, before any spend. |
| **Calibration-refresh seam** | Nightly APScheduler job that fetches each chip's calibration JSON and writes it to `~/.local/share/heisenberg/calibration/<chip>.json`. The compiler picks up the new file on its next compile. |
| **Verbatim submission** | The submission-adapter contract: jobs go to providers in pass-through mode. `skip_transpilation=True` for IBM, `Program(source=qasm_text)` for Braket, raw `openqasm-v3` for Azure. Rule 5. |
| **Spinor / Phonon / Photon source** | Three accepted `source_kind` values for `POST /api/v1/jobs`. Phonon is the engine's native input; Spinor wraps as a trivial Phonon program; Photon is lowered via the C++ frontend. |
| **`ResourceEstimate`** | `{ num_qubits, depth, two_qubit_count, t_count }` — what the compiler returns and what the cost seam consults. |
| **Histogram** | `{ counts: { bitstring: n }, shots: total }` — what the worker stores in `results.counts`. |
| **Lease** | The `claim_expires_at` window during which a worker holds a Running job. If the worker dies, another reclaims after the lease expires. |
| **API key** | `prefix.body` plaintext returned once on creation; bcrypt-hashed thereafter; identified by the 8-character prefix. |
| **Cassette mode** | `SPINOR_SUBMIT_MODE=cassette`; the submission adapter replays recorded JSON instead of hitting a provider. CI / dev default. |
| **Lease reclaim** | Sweep done by `worker.process_one` and `queue.reclaim_expired` that pushes Running jobs whose `claim_expires_at < now()` back to Queued. |
| **Drift** | A calibration refresh whose new error rates differ from the previous file by more than `relative_threshold` (default 50%). Surfaced in the log line and the `calibration_refresh_total` counter. |

## Historical-only terms

Inside the [build journal](build_journal.md) and the
[archive](https://github.com/nimesh08/quantum-stack/tree/main/docs/archive)
you may see the historical milestone vocabulary the project was
built under (alphabetic phase letters and numeric milestone tags).
Those names map onto the layered architecture as: the first phase
became Spinor, the second became Phonon, the third became Photon,
the fourth became the platform. User-facing docs use the layered
names instead.
