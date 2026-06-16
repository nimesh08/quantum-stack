import { useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { Link } from "react-router-dom";
import { api } from "../api/jobs";

export default function Settings() {
  const qc = useQueryClient();
  const { data, isLoading } = useQuery({
    queryKey: ["budget"],
    queryFn: api.budget,
  });
  const [daily, setDaily] = useState("");
  const [monthly, setMonthly] = useState("");
  const [maxShots, setMaxShots] = useState("");
  if (isLoading || !data) return <div>loading…</div>;
  const save = async () => {
    const body: Record<string, unknown> = {};
    if (daily) body.daily_usd = daily;
    if (monthly) body.monthly_usd = monthly;
    if (maxShots) body.max_shots_per_job = Number(maxShots);
    await api.patchBudget(body);
    qc.invalidateQueries({ queryKey: ["budget"] });
  };
  return (
    <div className="app">
      <header className="topbar">
        <h1>Settings</h1>
        <span className="spacer" />
        <Link to="/">Playground</Link>
      </header>
      <main style={{ padding: 16, maxWidth: 480 }}>
        <div className="panel">
          <strong>Budget</strong>
          <div className="kv" style={{ margin: "0.5rem 0 1rem" }}>
            <div>daily</div><div>${data.daily_usd}</div>
            <div>monthly</div><div>${data.monthly_usd}</div>
            <div>max shots/job</div><div>{data.max_shots_per_job}</div>
          </div>
          <input placeholder="new daily ($)"
            value={daily} onChange={(e) => setDaily(e.target.value)} />{" "}
          <input placeholder="new monthly ($)"
            value={monthly} onChange={(e) => setMonthly(e.target.value)} />{" "}
          <input placeholder="new max shots"
            value={maxShots} onChange={(e) => setMaxShots(e.target.value)} />{" "}
          <button onClick={save}>Save</button>
        </div>
      </main>
    </div>
  );
}
