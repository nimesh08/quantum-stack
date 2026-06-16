import { jsx as _jsx, jsxs as _jsxs, Fragment as _Fragment } from "react/jsx-runtime";
import { useState } from "react";
import { Link, useNavigate } from "react-router-dom";
import Editor from "../components/Editor";
import Histogram from "../components/Histogram";
import TargetPicker from "../components/TargetPicker";
import { api } from "../api/jobs";
import { useJobPoll } from "../hooks/useJob";
import { useAuth } from "../hooks/useAuth";
const BELL = `target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
`;
export default function Playground() {
    const [program, setProgram] = useState(BELL);
    const [kind, setKind] = useState("spinor");
    const [target, setTarget] = useState("generic");
    const [shots, setShots] = useState(1000);
    const [activeJobId, setActiveJobId] = useState(null);
    const [error, setError] = useState(null);
    const auth = useAuth();
    const nav = useNavigate();
    const { job, polling } = useJobPoll(activeJobId);
    const run = async () => {
        setError(null);
        try {
            const created = await api.createJob({
                source: program,
                source_kind: kind,
                target,
                shots,
            });
            setActiveJobId(created.id);
        }
        catch (exc) {
            if (exc?.status === 402 && exc?.detail) {
                const d = exc.detail;
                setError(`Over budget: ${d.reason}. Cost $${d.dollar_cost} ` +
                    `vs daily remaining $${d.daily_remaining ?? "?"}.`);
            }
            else if (exc?.status === 400 && exc?.detail) {
                setError(`Compile failed: ${JSON.stringify(exc.detail)}`);
            }
            else {
                setError(String(exc?.detail ?? exc?.message ?? exc));
            }
        }
    };
    const logout = () => {
        auth.clear();
        nav("/login");
    };
    return (_jsxs("div", { className: "app", children: [_jsxs("header", { className: "topbar", children: [_jsx("h1", { children: "Heisenberg Playground" }), _jsxs("select", { "data-testid": "kind-picker", value: kind, onChange: (e) => setKind(e.target.value), children: [_jsx("option", { value: "spinor", children: "spinor" }), _jsx("option", { value: "phonon", children: "phonon" }), _jsx("option", { value: "photon", children: "photon" })] }), _jsx(TargetPicker, { value: target, onChange: setTarget }), _jsx("input", { "data-testid": "shots-input", type: "number", min: 1, value: shots, onChange: (e) => setShots(Number(e.target.value)), style: { width: 100 } }), _jsx("button", { className: "primary", onClick: run, disabled: polling, "data-testid": "run-button", children: polling ? "Running…" : "Run" }), _jsx("span", { className: "spacer" }), _jsx(Link, { to: "/jobs", children: "Jobs" }), _jsx(Link, { to: "/settings", children: "Settings" }), _jsx("button", { onClick: logout, children: "Logout" })] }), _jsxs("main", { className: "split", children: [_jsx("section", { className: "editor-pane", children: _jsx(Editor, { value: program, kind: kind, onChange: setProgram }) }), _jsxs("section", { className: "right-pane", children: [error && _jsx("div", { className: "banner error", children: error }), job && (_jsxs("div", { className: "panel", children: [_jsxs("strong", { children: ["Job ", job.id.slice(0, 8), "\u2026"] }), _jsxs("div", { className: "kv", children: [_jsx("div", { children: "state" }), _jsx("div", { children: job.state }), _jsx("div", { children: "target" }), _jsx("div", { children: job.target }), _jsx("div", { children: "shots" }), _jsx("div", { children: job.shots }), job.estimate && (_jsxs(_Fragment, { children: [_jsx("div", { children: "qubits" }), _jsx("div", { children: job.estimate.num_qubits }), _jsx("div", { children: "2-qubit gates" }), _jsx("div", { children: job.estimate.two_qubit_count }), _jsx("div", { children: "depth" }), _jsx("div", { children: job.estimate.depth })] })), job.dollar_cost && (_jsxs(_Fragment, { children: [_jsx("div", { children: "cost" }), _jsxs("div", { children: ["$", job.dollar_cost] })] })), job.rejection_reason && (_jsxs(_Fragment, { children: [_jsx("div", { children: "reason" }), _jsx("div", { children: job.rejection_reason })] }))] })] })), job?.result && (_jsxs("div", { className: "panel", children: [_jsxs("strong", { children: ["Histogram (", job.result.shots, " shots)"] }), _jsx(Histogram, { counts: job.result.counts, shots: job.result.shots })] }))] })] })] }));
}
