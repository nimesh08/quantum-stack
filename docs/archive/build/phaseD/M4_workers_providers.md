# M4 — Workers + provider adapters

End: a job that reaches `Queued` is picked up by a worker, submitted
to its provider in verbatim mode, and its histogram lands in the
`results` table.

## Queue

`jobsvc.queue` implements Postgres-as-queue (D1):

- `enqueue` — `NOTIFY jobs_new, '<job_id>'` inside the same
  transaction as `state=Queued`. SQLite tests skip the NOTIFY.
- `claim` — `SELECT … FROM jobs WHERE state='Queued' ORDER BY queued_at
  FOR UPDATE SKIP LOCKED LIMIT 1` then atomically transition to
  `Running` with `claimed_by` and `claim_expires_at`.
- `reclaim_expired` — Running jobs whose lease expired return to
  Queued so another worker picks them up.

## Provider routing

`jobsvc.providers.submit_to_provider`:

| chip.provider | path |
|---|---|
| `local` | in-process simulator (Bell-like for circuits with a 2-qubit gate) |
| `ibm` | `spinor_submit.submit(qasm, chip, provider="ibm")` |
| `aws` | `spinor_submit.submit(qasm, chip, provider="aws")` |
| `azure` | `spinor_submit.submit(qasm, chip, provider="azure")` |

`SPINOR_SUBMIT_MODE` (`cassette`/`live`/`local`) is honoured by the
Phase A adapter; CI runs `cassette`.

## Failure classification (M7 prep)

`error_kind` is one of `our` (compile error, unknown chip) or
`provider` (adapter raised). The Prometheus counter
`errors_total{kind=...}` increments accordingly.

## Tests landed

- `tests/integration/test_worker.py` (7 cases) — local e2e Bell;
  empty-queue claim; multi-claim ordering; lease expiry reclaim;
  provider-error classification; our-error classification; cancel
  before claim.
- `tests/regression/test_cassette_submit.py` — full path through
  `spinor_submit` cassette mode against the IBM bell cassette.

75/75 green at end of M4 (37 unit + 30 integration + cassette + a few
extras).
