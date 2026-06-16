/**
 * Typed `fetch` wrapper for the Phase D job-service REST API.
 *
 * Every function attaches the bearer JWT from local-storage on the
 * outbound request; on `401 Unauthorized` it clears the token and
 * throws so the caller can redirect to /login. On `4xx` it preserves
 * the FastAPI `detail` payload on the thrown error's `detail` field —
 * the playground uses that to surface structured 402 banners.
 *
 * @packageDocumentation
 */

import { getStoredAccessToken } from "../hooks/useAuth";

/**
 * Resource estimate from the compiler — what the cost-control seam
 * compares against the user's budget.
 */
export type Estimate = {
  /** Number of qubits the program allocates. */
  num_qubits: number;
  /** Linear-depth proxy: count of non-allocation ops. */
  depth: number;
  /**
   * Number of two-qubit gates (cx/cz/swap/ecr/ms/rzz). The dominant
   * error source on real hardware.
   */
  two_qubit_count: number;
  /** Number of T / Tdg gates (magic-state count). */
  t_count: number;
};

/**
 * Compact job view (no source, no result) — what the API returns
 * from `GET /jobs` and `POST /jobs`.
 */
export type Job = {
  /** UUID of the job. */
  id: string;
  /** UUID of the owning user. */
  user_id: string;
  /** Optional human label. */
  name: string;
  /** Chip id (`generic`, `ibm_heron_r2`, …). */
  target: string;
  /** Shot count requested. */
  shots: number;
  /** Source language. */
  source_kind: "photon" | "phonon" | "spinor";
  /** Lifecycle state. */
  state:
    | "Submitted"
    | "Queued"
    | "Running"
    | "Completed"
    | "Rejected"
    | "Failed";
  /** Free-text rejection / failure explanation, if any. */
  rejection_reason: string | null;
  /**
   * `"our"` for compile / queue / unknown-chip errors, `"provider"`
   * for adapter exceptions. Only set on Failed jobs.
   */
  error_kind: string | null;
  /** Resource estimate, available once compile succeeds. */
  estimate: Estimate | null;
  /**
   * Estimated dollar cost as a string (Decimal — preserves
   * microdollar precision across the JSON boundary).
   */
  dollar_cost: string | null;
  /** `ibm | aws | azure | local`, derived from the chip YAML. */
  provider: string | null;
  /** ISO-8601 timestamps. */
  created_at: string;
  queued_at: string | null;
  started_at: string | null;
  finished_at: string | null;
};

/**
 * Full job view: `Job` plus the source program and the histogram
 * (when Completed). Returned by `GET /jobs/{id}`.
 */
export type JobDetail = Job & {
  source: string;
  /**
   * Measurement histogram. `counts` maps bitstrings to shot counts;
   * `shots` is the total. `null` until the job is Completed.
   */
  result: { counts: Record<string, number>; shots: number } | null;
};

/** One row of `GET /targets` — a chip the platform can submit to. */
export type Target = {
  /** Chip id. */
  id: string;
  /** Submission provider. */
  provider: string;
  /** Number of qubits. */
  qubits: number;
  /** Native gate set the chip exposes. */
  native_gates: string[];
  /** Topology name (e.g. `heavy_hex_156`, `all_to_all`). */
  coupling_topology: string;
  /** Capability flags from the chip YAML (`mid_circuit_measure`, …). */
  supports: Record<string, unknown>;
  /** Per-shot price in USD; the cost-control seam uses this. */
  pricing_per_shot_usd: number;
};

const BASE = "/api/v1";

/**
 * Internal: typed `fetch` wrapper.
 *
 * @param path - Path relative to `/api/v1`.
 * @param init - Standard fetch init (method, body, headers).
 * @returns Parsed JSON, or `undefined` for empty bodies.
 * @throws `Error & { status, detail }` on any non-2xx; on 401 also
 *   clears the local-storage access token.
 */
async function request<T>(
  path: string,
  init: RequestInit = {},
): Promise<T> {
  const token = getStoredAccessToken();
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
    ...((init.headers as Record<string, string>) ?? {}),
  };
  if (token) headers.Authorization = `Bearer ${token}`;
  const res = await fetch(BASE + path, { ...init, headers });
  if (res.status === 401) {
    localStorage.removeItem("access_token");
    throw new Error("unauthorized");
  }
  const text = await res.text();
  if (!res.ok) {
    let detail: unknown = text;
    try {
      detail = JSON.parse(text)?.detail ?? detail;
    } catch {
      // not JSON
    }
    throw Object.assign(new Error("api error"), { status: res.status, detail });
  }
  return text ? (JSON.parse(text) as T) : (undefined as T);
}

/**
 * Typed client for `/api/v1/*`. Every method attaches the bearer
 * JWT from local-storage; 401 clears the token and throws.
 *
 * @example
 * ```ts
 * const tokens = await api.login("user@x.com", "pwd");
 * localStorage.setItem("access_token", tokens.access_token);
 * const job = await api.createJob({
 *   source: "target generic\\nqubit q[2]\\nh q[0]\\ncx q[0], q[1]\\n",
 *   source_kind: "spinor",
 *   target: "generic",
 *   shots: 100,
 * });
 * ```
 */
export const api = {
  /**
   * Exchange email + password for a JWT pair.
   * @throws `401` on bad credentials.
   */
  login: (email: string, password: string) =>
    request<{ access_token: string; refresh_token: string; expires_in: number }>(
      "/login",
      { method: "POST", body: JSON.stringify({ email, password }) },
    ),
  /** Current user. Requires a valid JWT or API key. */
  me: () => request<{ id: string; email: string; role: string }>("/me"),
  /** Read the caller's budget (auto-creates default if absent). */
  budget: () =>
    request<{
      daily_usd: string;
      monthly_usd: string;
      max_shots_per_job: number;
    }>("/me/budget"),
  /** Update one or more budget fields. */
  patchBudget: (body: Record<string, unknown>) =>
    request("/me/budget", { method: "PATCH", body: JSON.stringify(body) }),
  /** List every chip the registry knows about. */
  targets: () => request<Target[]>("/targets"),
  /**
   * Submit a job. Compiles, runs the cost check, queues if allowed.
   * @throws `400` on compile error, `402` on over-budget.
   */
  createJob: (body: {
    source: string;
    source_kind: "photon" | "phonon" | "spinor";
    target: string;
    shots: number;
    name?: string;
  }) => request<Job>("/jobs", { method: "POST", body: JSON.stringify(body) }),
  /** Fetch a single job by id (incl. source + result if available). */
  getJob: (id: string) => request<JobDetail>(`/jobs/${id}`),
  /** Recent 50 jobs of the caller. */
  listJobs: () =>
    request<{ items: Job[]; next_cursor: string | null }>("/jobs?limit=50"),
  /** Cancel a Submitted/Queued job. 409 if the worker already claimed it. */
  cancelJob: (id: string) => request(`/jobs/${id}`, { method: "DELETE" }),
};
