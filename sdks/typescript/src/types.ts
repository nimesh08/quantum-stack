/**
 * Domain types for `@heisenberg/sdk`.
 *
 * These mirror the `jobsvc` Pydantic schemas. They are produced
 * by hand today; the release pipeline will eventually generate
 * them from `jobsvc`'s OpenAPI export.
 *
 * Author: Nimesh Cheedella <chnimesh0808@gmail.com>
 */

/** Source language of a kernel. */
export type SourceKind = "photon" | "phonon" | "spinor";

/** Lifecycle states the queue may report. */
export type JobState =
  | "Submitted"
  | "Queued"
  | "Running"
  | "Completed"
  | "Rejected"
  | "Failed";

/**
 * Resource estimate from the compiler — what the cost-control
 * seam compares against the user's budget.
 */
export interface Estimate {
  num_qubits: number;
  depth: number;
  two_qubit_count: number;
  t_count: number;
}

/** Compact job view (no source, no result). */
export interface Job {
  id: string;
  user_id: string;
  name: string;
  target: string;
  shots: number;
  source_kind: SourceKind;
  state: JobState;
  rejection_reason: string | null;
  error_kind: string | null;
  estimate: Estimate | null;
  dollar_cost: string | null;
  provider: string | null;
  created_at: string;
  queued_at: string | null;
  started_at: string | null;
  finished_at: string | null;
}

/** Full job view: `Job` plus source + histogram (when Completed). */
export interface JobDetail extends Job {
  source: string;
  result: { counts: Record<string, number>; shots: number } | null;
}

/** One row of `GET /api/v1/targets`. */
export interface Target {
  id: string;
  provider: string;
  qubits: number;
  native_gates: string[];
  coupling_topology: string;
  supports: Record<string, unknown>;
  pricing_per_shot_usd: number;
}

/** Cursor-paged collection. */
export interface Page<T> {
  items: T[];
  next_cursor: string | null;
}

/** Body of `POST /api/v1/jobs`. */
export interface CreateJobBody {
  source: string;
  source_kind: SourceKind;
  target: string;
  shots: number;
  name?: string;
}

/** JWT login response. */
export interface TokenPair {
  access_token: string;
  refresh_token: string;
  expires_in: number;
}

/** Newly minted API key. The `plaintext` is shown once. */
export interface ApiKeyCreated {
  id: string;
  prefix: string;
  plaintext: string;
  label: string;
  created_at: string;
}

/** Per-user budget. Decimals are JSON strings to preserve precision. */
export interface Budget {
  daily_usd: string;
  monthly_usd: string;
  max_shots_per_job: number;
}

/** Error thrown by the SDK on any non-2xx response. */
export class HeisenbergApiError extends Error {
  /** HTTP status code. */
  readonly status: number;
  /**
   * The `detail` field of the FastAPI error envelope, when present.
   * On a `402` it includes the budget breakdown.
   */
  readonly detail: unknown;
  /** Echoed `x-request-id` header. */
  readonly requestId?: string;

  constructor(opts: {
    message: string;
    status: number;
    detail?: unknown;
    requestId?: string;
  }) {
    super(opts.message);
    this.name = "HeisenbergApiError";
    this.status = opts.status;
    this.detail = opts.detail;
    if (opts.requestId !== undefined) {
      this.requestId = opts.requestId;
    }
  }
}
