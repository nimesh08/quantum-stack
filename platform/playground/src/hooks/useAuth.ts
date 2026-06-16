import { create } from "zustand";

type AuthState = {
  accessToken: string | null;
  refreshToken: string | null;
  email: string | null;
  setTokens: (a: string, r: string, email: string) => void;
  clear: () => void;
};

const useAuthStore = create<AuthState>((set) => ({
  accessToken: localStorage.getItem("access_token"),
  refreshToken: localStorage.getItem("refresh_token"),
  email: localStorage.getItem("email"),
  setTokens: (a, r, email) => {
    localStorage.setItem("access_token", a);
    localStorage.setItem("refresh_token", r);
    localStorage.setItem("email", email);
    set({ accessToken: a, refreshToken: r, email });
  },
  clear: () => {
    localStorage.removeItem("access_token");
    localStorage.removeItem("refresh_token");
    localStorage.removeItem("email");
    set({ accessToken: null, refreshToken: null, email: null });
  },
}));

/**
 * Auth state hook. Reads tokens from local-storage at first render,
 * exposes `setTokens` to replace them and `clear` to log out.
 *
 * @returns `{ isAuthed, accessToken, email, setTokens, clear }`.
 *
 * @example
 * ```tsx
 * const auth = useAuth();
 * if (!auth.isAuthed) return <Navigate to="/login" />;
 * ```
 */
export function useAuth() {
  const s = useAuthStore();
  return {
    isAuthed: Boolean(s.accessToken),
    accessToken: s.accessToken,
    email: s.email,
    setTokens: s.setTokens,
    clear: s.clear,
  };
}

/**
 * Read the bearer token from local-storage (sync).
 *
 * Used by the {@link api} client outside the React tree, where the
 * Zustand store isn't reachable. Returns `null` if no token is
 * stored.
 */
export function getStoredAccessToken(): string | null {
  return localStorage.getItem("access_token");
}
