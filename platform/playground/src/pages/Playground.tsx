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

type Kind = "photon" | "phonon" | "spinor";

export default function Playground() {
  const [program, setProgram] = useState(BELL);
  const [kind, setKind] = useState<Kind>("spinor");
  const [target, setTarget] = useState("generic");
  const [shots, setShots] = useState(1000);
  const [activeJobId, setActiveJobId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const auth = useAuth();
  const nav = useNavigate();
  const { job, polling } = useJobPoll(activeJobId);

  // Test seam: e2e tests inject a program via `window.__setProgram`.
  // Inert in production (no test runner sets it).
  if (typeof window !== "undefined") {
    (window as any).__setProgram = setProgram;
  }

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
    } catch (exc: any) {
      if (exc?.status === 402 && exc?.detail) {
        const d = exc.detail;
        setError(
          `Over budget: ${d.reason}. Cost $${d.dollar_cost} ` +
            `vs daily remaining $${d.daily_remaining ?? "?"}.`,
        );
      } else if (exc?.status === 400 && exc?.detail) {
        setError(`Compile failed: ${JSON.stringify(exc.detail)}`);
      } else {
        setError(String(exc?.detail ?? exc?.message ?? exc));
      }
    }
  };

  const logout = () => {
    auth.clear();
    nav("/login");
  };

  return (
    <div className="app">
      <header className="topbar">
        <h1>Heisenberg Playground</h1>
        <select
          data-testid="kind-picker"
          value={kind}
          onChange={(e) => setKind(e.target.value as Kind)}
        >
          <option value="spinor">spinor</option>
          <option value="phonon">phonon</option>
          <option value="photon">photon</option>
        </select>
        <TargetPicker value={target} onChange={setTarget} />
        <input
          data-testid="shots-input"
          type="number"
          min={1}
          value={shots}
          onChange={(e) => setShots(Number(e.target.value))}
          style={{ width: 100 }}
        />
        <button
          className="primary"
          onClick={run}
          disabled={polling}
          data-testid="run-button"
        >
          {polling ? "Running…" : "Run"}
        </button>
        <span className="spacer" />
        <Link to="/jobs">Jobs</Link>
        <Link to="/settings">Settings</Link>
        <button onClick={logout}>Logout</button>
      </header>
      <main className="split">
        <section className="editor-pane">
          <Editor value={program} kind={kind} onChange={setProgram} />
        </section>
        <section className="right-pane">
          {error && <div className="banner error">{error}</div>}
          {job && (
            <div className="panel">
              <strong>Job {job.id.slice(0, 8)}…</strong>
              <div className="kv">
                <div>state</div><div>{job.state}</div>
                <div>target</div><div>{job.target}</div>
                <div>shots</div><div>{job.shots}</div>
                {job.estimate && (
                  <>
                    <div>qubits</div><div>{job.estimate.num_qubits}</div>
                    <div>2-qubit gates</div><div>{job.estimate.two_qubit_count}</div>
                    <div>depth</div><div>{job.estimate.depth}</div>
                  </>
                )}
                {job.dollar_cost && (
                  <><div>cost</div><div>${job.dollar_cost}</div></>
                )}
                {job.rejection_reason && (
                  <><div>reason</div><div>{job.rejection_reason}</div></>
                )}
              </div>
            </div>
          )}
          {job?.result && (
            <div className="panel">
              <strong>Histogram ({job.result.shots} shots)</strong>
              <Histogram counts={job.result.counts} shots={job.result.shots} />
            </div>
          )}
        </section>
      </main>
    </div>
  );
}
