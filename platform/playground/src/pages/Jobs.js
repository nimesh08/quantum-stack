import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { useQuery } from "@tanstack/react-query";
import { Link } from "react-router-dom";
import { api } from "../api/jobs";
export default function Jobs() {
    const { data, isLoading } = useQuery({
        queryKey: ["jobs"],
        queryFn: api.listJobs,
        refetchInterval: 5000,
    });
    if (isLoading)
        return _jsx("div", { children: "loading\u2026" });
    const items = data?.items ?? [];
    return (_jsxs("div", { className: "app", children: [_jsxs("header", { className: "topbar", children: [_jsx("h1", { children: "Jobs" }), _jsx("span", { className: "spacer" }), _jsx(Link, { to: "/", children: "Playground" })] }), _jsx("main", { style: { padding: 16, overflow: "auto" }, children: _jsxs("table", { style: { width: "100%", borderCollapse: "collapse" }, children: [_jsx("thead", { children: _jsxs("tr", { children: [_jsx("th", { align: "left", children: "id" }), _jsx("th", { align: "left", children: "state" }), _jsx("th", { align: "left", children: "target" }), _jsx("th", { align: "right", children: "shots" }), _jsx("th", { align: "right", children: "cost" }), _jsx("th", { align: "left", children: "created" })] }) }), _jsx("tbody", { children: items.map((j) => (_jsxs("tr", { children: [_jsxs("td", { children: [j.id.slice(0, 8), "\u2026"] }), _jsx("td", { children: j.state }), _jsx("td", { children: j.target }), _jsx("td", { align: "right", children: j.shots }), _jsx("td", { align: "right", children: j.dollar_cost ?? "0" }), _jsx("td", { children: new Date(j.created_at).toLocaleString() })] }, j.id))) })] }) })] }));
}
