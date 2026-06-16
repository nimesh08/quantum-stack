import { useQuery } from "@tanstack/react-query";
import { Link } from "react-router-dom";
import { api } from "../api/jobs";

export default function Jobs() {
  const { data, isLoading } = useQuery({
    queryKey: ["jobs"],
    queryFn: api.listJobs,
    refetchInterval: 5000,
  });
  if (isLoading) return <div>loading…</div>;
  const items = data?.items ?? [];
  return (
    <div className="app">
      <header className="topbar">
        <h1>Jobs</h1>
        <span className="spacer" />
        <Link to="/">Playground</Link>
      </header>
      <main style={{ padding: 16, overflow: "auto" }}>
        <table style={{ width: "100%", borderCollapse: "collapse" }}>
          <thead>
            <tr>
              <th align="left">id</th>
              <th align="left">state</th>
              <th align="left">target</th>
              <th align="right">shots</th>
              <th align="right">cost</th>
              <th align="left">created</th>
            </tr>
          </thead>
          <tbody>
            {items.map((j) => (
              <tr key={j.id}>
                <td>{j.id.slice(0, 8)}…</td>
                <td>{j.state}</td>
                <td>{j.target}</td>
                <td align="right">{j.shots}</td>
                <td align="right">{j.dollar_cost ?? "0"}</td>
                <td>{new Date(j.created_at).toLocaleString()}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </main>
    </div>
  );
}
