import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { useState } from "react";
import { useNavigate } from "react-router-dom";
import { api } from "../api/jobs";
import { useAuth } from "../hooks/useAuth";
export default function Login() {
    const [email, setEmail] = useState("");
    const [password, setPassword] = useState("");
    const [err, setErr] = useState(null);
    const auth = useAuth();
    const nav = useNavigate();
    const onSubmit = async (e) => {
        e.preventDefault();
        setErr(null);
        try {
            const { access_token, refresh_token } = await api.login(email, password);
            auth.setTokens(access_token, refresh_token, email);
            nav("/");
        }
        catch (exc) {
            setErr(String(exc?.detail ?? "login failed"));
        }
    };
    return (_jsxs("form", { className: "login", onSubmit: onSubmit, children: [_jsx("h1", { children: "Heisenberg Playground" }), _jsx("input", { "data-testid": "login-email", placeholder: "email", value: email, onChange: (e) => setEmail(e.target.value), autoFocus: true }), _jsx("input", { "data-testid": "login-password", type: "password", placeholder: "password", value: password, onChange: (e) => setPassword(e.target.value) }), err && _jsx("div", { className: "banner error", children: err }), _jsx("button", { "data-testid": "login-submit", type: "submit", children: "Sign in" })] }));
}
