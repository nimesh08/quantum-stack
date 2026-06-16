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

export function getStoredAccessToken(): string | null {
  return localStorage.getItem("access_token");
}
