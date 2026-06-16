import { FormEvent, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/jobs";
import { useAuth } from "../hooks/useAuth";

export default function Login() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const auth = useAuth();
  const nav = useNavigate();

  const onSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setErr(null);
    try {
      const { access_token, refresh_token } = await api.login(email, password);
      auth.setTokens(access_token, refresh_token, email);
      nav("/");
    } catch (exc: any) {
      setErr(String(exc?.detail ?? "login failed"));
    }
  };

  return (
    <form className="login" onSubmit={onSubmit}>
      <h1>Heisenberg Playground</h1>
      <input
        data-testid="login-email"
        placeholder="email"
        value={email}
        onChange={(e) => setEmail(e.target.value)}
        autoFocus
      />
      <input
        data-testid="login-password"
        type="password"
        placeholder="password"
        value={password}
        onChange={(e) => setPassword(e.target.value)}
      />
      {err && <div className="banner error">{err}</div>}
      <button data-testid="login-submit" type="submit">
        Sign in
      </button>
    </form>
  );
}
