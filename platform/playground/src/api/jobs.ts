import { getStoredAccessToken } from "../hooks/useAuth";

export type Estimate = {
  num_qubits: number;
  depth: number;
  two_qubit_count: number;
  t_count: number;
};

export type Job = {
  id: string;
  user_id: string;
  name: string;
  target: string;
  shots: number;
  source_kind: "photon" | "phonon" | "spinor";
  state:
    | "Submitted"
    | "Queued"
    | "Running"
    | "Completed"
    | "Rejected"
    | "Failed";
  rejection_reason: string | null;
  error_kind: string | null;
  estimate: Estimate | null;
  dollar_cost: string | null;
  provider: string | null;
  created_at: string;
  queued_at: string | null;
  started_at: string | null;
  finished_at: string | null;
};

export type JobDetail = Job & {
  source: string;
  result: { counts: Record<string, number>; shots: number } | null;
};

export type Target = {
  id: string;
  provider: string;
  qubits: number;
  native_gates: string[];
  coupling_topology: string;
  supports: Record<string, unknown>;
  pricing_per_shot_usd: number;
};

const BASE = "/api/v1";

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

export const api = {
  login: (email: string, password: string) =>
    request<{ access_token: string; refresh_token: string; expires_in: number }>(
      "/login",
      { method: "POST", body: JSON.stringify({ email, password }) },
    ),
  me: () => request<{ id: string; email: string; role: string }>("/me"),
  budget: () =>
    request<{
      daily_usd: string;
      monthly_usd: string;
      max_shots_per_job: number;
    }>("/me/budget"),
  patchBudget: (body: Record<string, unknown>) =>
    request("/me/budget", { method: "PATCH", body: JSON.stringify(body) }),
  targets: () => request<Target[]>("/targets"),
  createJob: (body: {
    source: string;
    source_kind: "photon" | "phonon" | "spinor";
    target: string;
    shots: number;
    name?: string;
  }) => request<Job>("/jobs", { method: "POST", body: JSON.stringify(body) }),
  getJob: (id: string) => request<JobDetail>(`/jobs/${id}`),
  listJobs: () => request<{ items: Job[]; next_cursor: string | null }>(
    "/jobs?limit=50",
  ),
  cancelJob: (id: string) => request(`/jobs/${id}`, { method: "DELETE" }),
};
