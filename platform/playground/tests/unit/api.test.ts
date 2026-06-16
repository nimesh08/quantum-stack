import { describe, expect, it, beforeEach, vi, afterEach } from "vitest";
import { api } from "../../src/api/jobs";

describe("api.jobs", () => {
  let originalFetch: typeof fetch;

  beforeEach(() => {
    originalFetch = global.fetch;
    localStorage.setItem("access_token", "fake-token");
  });
  afterEach(() => {
    global.fetch = originalFetch;
    localStorage.clear();
    vi.restoreAllMocks();
  });

  it("attaches the bearer token to authenticated requests", async () => {
    const spy = vi.fn(async (_url: any, init: any) => {
      expect(init.headers.Authorization).toBe("Bearer fake-token");
      return new Response(JSON.stringify({ id: "abc" }), { status: 200 });
    });
    global.fetch = spy as unknown as typeof fetch;
    await api.getJob("abc");
    expect(spy).toHaveBeenCalled();
  });

  it("clears the token on 401", async () => {
    global.fetch = (async () =>
      new Response("", { status: 401 })) as unknown as typeof fetch;
    await expect(api.getJob("any")).rejects.toThrow("unauthorized");
    expect(localStorage.getItem("access_token")).toBeNull();
  });

  it("propagates 402 detail for over-budget rejection", async () => {
    global.fetch = (async () =>
      new Response(
        JSON.stringify({
          detail: { reason: "exceeds_daily_budget", dollar_cost: "0.33" },
        }),
        { status: 402 },
      )) as unknown as typeof fetch;
    try {
      await api.createJob({
        source: "x",
        source_kind: "spinor",
        target: "ibm_heron_r2",
        shots: 1000,
      });
      expect.fail("should have thrown");
    } catch (exc: any) {
      expect(exc.status).toBe(402);
      expect(exc.detail.reason).toBe("exceeds_daily_budget");
    }
  });
});
