import { getStoredAccessToken } from "../hooks/useAuth";
const BASE = "/api/v1";
async function request(path, init = {}) {
    const token = getStoredAccessToken();
    const headers = {
        "Content-Type": "application/json",
        ...(init.headers ?? {}),
    };
    if (token)
        headers.Authorization = `Bearer ${token}`;
    const res = await fetch(BASE + path, { ...init, headers });
    if (res.status === 401) {
        localStorage.removeItem("access_token");
        throw new Error("unauthorized");
    }
    const text = await res.text();
    if (!res.ok) {
        let detail = text;
        try {
            detail = JSON.parse(text)?.detail ?? detail;
        }
        catch {
            // not JSON
        }
        throw Object.assign(new Error("api error"), { status: res.status, detail });
    }
    return text ? JSON.parse(text) : undefined;
}
export const api = {
    login: (email, password) => request("/login", { method: "POST", body: JSON.stringify({ email, password }) }),
    me: () => request("/me"),
    budget: () => request("/me/budget"),
    patchBudget: (body) => request("/me/budget", { method: "PATCH", body: JSON.stringify(body) }),
    targets: () => request("/targets"),
    createJob: (body) => request("/jobs", { method: "POST", body: JSON.stringify(body) }),
    getJob: (id) => request(`/jobs/${id}`),
    listJobs: () => request("/jobs?limit=50"),
    cancelJob: (id) => request(`/jobs/${id}`, { method: "DELETE" }),
};
