/**
 * Typed `fetch` wrapper for the Heisenberg jobsvc REST API.
 *
 * The canonical types and a higher-level client (with React Query
 * hooks) live in `@heisenberg/sdk`; this file re-exports those
 * types and keeps the playground's bespoke `fetch` wrapper for the
 * cookie/JWT path the SPA uses today. New consumers should prefer
 * `@heisenberg/sdk` directly.
 *
 * Author: Nimesh Cheedella.
 *
 * @packageDocumentation
 */

import { getStoredAccessToken } from "../hooks/useAuth";

export type {
  Estimate,
  Job,
  JobDetail,
  Target,
  SourceKind,
  JobState,
} from "@heisenberg/sdk";

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
