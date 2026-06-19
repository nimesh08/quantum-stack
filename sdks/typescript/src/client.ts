/**
 * Low-level typed client. Wraps `fetch` and turns non-2xx responses
 * into a `HeisenbergApiError`. Authentication is via a long-lived
 * API key (preferred) or a bearer JWT.
 *
 * Author: Nimesh Cheedella <chnimesh0808@gmail.com>
 */

import {
  ApiKeyCreated,
  Budget,
  CreateJobBody,
  HeisenbergApiError,
  Job,
  JobDetail,
  Page,
  Target,
  TokenPair,
} from "./types.js";

/**
 * Configuration for {@link createClient}.
 *
 * Provide an `apiKey` for headless integrations (CI, scripts) or a
 * `getAccessToken` callback for browser apps that hold a JWT.
 */
export interface ClientOptions {
  /** Base URL of the jobsvc instance, e.g. `http://127.0.0.1:8080`. */
  baseUrl: string;
  /** Long-lived API key (preferred for server-side and scripts). */
  apiKey?: string;
  /**
   * Optional callback that returns the current bearer JWT. Called on
   * every request so the SDK can pick up token refreshes without
   * being re-instantiated.
   */
  getAccessToken?: () => string | null | undefined;
  /** Optional `fetch` override (e.g. for testing). */
  fetch?: typeof fetch;
}

interface RawClient {
  baseUrl: string;
  apiKey?: string;
  getAccessToken?: () => string | null | undefined;
  fetch: typeof fetch;
}

async function request<T>(
  raw: RawClient,
  path: string,
  init: RequestInit = {},
): Promise<T> {
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
    ...((init.headers as Record<string, string>) ?? {}),
  };
  if (raw.apiKey) {
    headers["X-API-Key"] = raw.apiKey;
  }
  const token = raw.getAccessToken?.();
  if (token) {
    headers.Authorization = `Bearer ${token}`;
  }
  const res = await raw.fetch(`${raw.baseUrl}${path}`, { ...init, headers });
  const reqId = res.headers.get("x-request-id") ?? undefined;
  const text = await res.text();
  if (!res.ok) {
    let detail: unknown = text;
    try {
      detail = JSON.parse(text)?.detail ?? detail;
    } catch {
      // body wasn't JSON; keep the raw text
    }
    throw new HeisenbergApiError({
      message: `heisenberg API ${res.status} on ${init.method ?? "GET"} ${path}`,
      status: res.status,
      detail,
      ...(reqId !== undefined ? { requestId: reqId } : {}),
    });
  }
  return text ? (JSON.parse(text) as T) : (undefined as T);
}

function noTrailingSlash(s: string): string {
  return s.endsWith("/") ? s.slice(0, -1) : s;
}

/**
 * Build a typed client against a jobsvc deployment.
 *
 * Every method returns a `Promise<T>`. Non-2xx responses throw
 * {@link HeisenbergApiError} with `status` and `detail` fields.
 */
export function createClient(options: ClientOptions) {
  const raw: RawClient = {
    baseUrl: noTrailingSlash(options.baseUrl),
    fetch: options.fetch ?? fetch,
    ...(options.apiKey !== undefined ? { apiKey: options.apiKey } : {}),
    ...(options.getAccessToken !== undefined
      ? { getAccessToken: options.getAccessToken }
      : {}),
  };

  return {
    /**
     * Liveness probe. Returns 200 with a small JSON body on the
     * default install.
     */
    healthz: () => request<{ status: string; version: string }>(raw, "/healthz"),

    /** Authenticated user. Requires JWT or API key. */
    me: {
      get: () =>
        request<{ id: string; email: string; role: string }>(
          raw,
          "/api/v1/me",
        ),
      budget: {
        get: () => request<Budget>(raw, "/api/v1/me/budget"),
        update: (body: Partial<Budget>) =>
          request<Budget>(raw, "/api/v1/me/budget", {
            method: "PATCH",
            body: JSON.stringify(body),
          }),
      },
      apiKeys: {
        create: (body: { label: string }) =>
          request<ApiKeyCreated>(raw, "/api/v1/me/api-keys", {
            method: "POST",
            body: JSON.stringify(body),
          }),
        revoke: (id: string) =>
          request<void>(raw, `/api/v1/me/api-keys/${id}`, {
            method: "DELETE",
          }),
      },
    },

    /** Authentication helpers. */
    auth: {
      login: (email: string, password: string) =>
        request<TokenPair>(raw, "/api/v1/login", {
          method: "POST",
          body: JSON.stringify({ email, password }),
        }),
      refresh: (refreshToken: string) =>
        request<TokenPair>(raw, "/api/v1/refresh", {
          method: "POST",
          body: JSON.stringify({ refresh_token: refreshToken }),
        }),
    },

    /** Chip targets known to this jobsvc instance. */
    targets: {
      list: () => request<Page<Target>>(raw, "/api/v1/targets"),
    },

    /** Job lifecycle. */
    jobs: {
      create: (body: CreateJobBody) =>
        request<Job>(raw, "/api/v1/jobs", {
          method: "POST",
          body: JSON.stringify(body),
        }),
      get: (id: string) => request<JobDetail>(raw, `/api/v1/jobs/${id}`),
      list: (opts: { limit?: number; cursor?: string } = {}) => {
        const params = new URLSearchParams();
        if (opts.limit !== undefined) params.set("limit", String(opts.limit));
        if (opts.cursor !== undefined) params.set("cursor", opts.cursor);
        const qs = params.toString();
        return request<Page<Job>>(
          raw,
          `/api/v1/jobs${qs ? `?${qs}` : ""}`,
        );
      },
      cancel: (id: string) =>
        request<void>(raw, `/api/v1/jobs/${id}/cancel`, { method: "POST" }),
    },
  };
}

/** Convenience type alias for the object that `createClient` returns. */
export type Client = ReturnType<typeof createClient>;
