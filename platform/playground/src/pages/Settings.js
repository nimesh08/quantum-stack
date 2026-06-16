import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
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
    if (isLoading || !data)
        return _jsx("div", { children: "loading\u2026" });
    const save = async () => {
        const body = {};
        if (daily)
            body.daily_usd = daily;
        if (monthly)
            body.monthly_usd = monthly;
        if (maxShots)
            body.max_shots_per_job = Number(maxShots);
        await api.patchBudget(body);
        qc.invalidateQueries({ queryKey: ["budget"] });
    };
    return (_jsxs("div", { className: "app", children: [_jsxs("header", { className: "topbar", children: [_jsx("h1", { children: "Settings" }), _jsx("span", { className: "spacer" }), _jsx(Link, { to: "/", children: "Playground" })] }), _jsx("main", { style: { padding: 16, maxWidth: 480 }, children: _jsxs("div", { className: "panel", children: [_jsx("strong", { children: "Budget" }), _jsxs("div", { className: "kv", style: { margin: "0.5rem 0 1rem" }, children: [_jsx("div", { children: "daily" }), _jsxs("div", { children: ["$", data.daily_usd] }), _jsx("div", { children: "monthly" }), _jsxs("div", { children: ["$", data.monthly_usd] }), _jsx("div", { children: "max shots/job" }), _jsx("div", { children: data.max_shots_per_job })] }), _jsx("input", { placeholder: "new daily ($)", value: daily, onChange: (e) => setDaily(e.target.value) }), " ", _jsx("input", { placeholder: "new monthly ($)", value: monthly, onChange: (e) => setMonthly(e.target.value) }), " ", _jsx("input", { placeholder: "new max shots", value: maxShots, onChange: (e) => setMaxShots(e.target.value) }), " ", _jsx("button", { onClick: save, children: "Save" })] }) })] }));
}
