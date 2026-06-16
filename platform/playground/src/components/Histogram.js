import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { Bar, BarChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
export default function Histogram({ counts, shots }) {
    const data = Object.entries(counts)
        .map(([k, v]) => ({ outcome: k, count: v, p: shots > 0 ? v / shots : 0 }))
        .sort((a, b) => b.count - a.count);
    return (_jsx("div", { className: "histogram", "data-testid": "histogram", children: _jsx(ResponsiveContainer, { width: "100%", height: "100%", children: _jsxs(BarChart, { data: data, margin: { left: 10, right: 10, top: 10, bottom: 10 }, children: [_jsx(XAxis, { dataKey: "outcome", stroke: "#94a3b8", fontSize: 12 }), _jsx(YAxis, { stroke: "#94a3b8", fontSize: 12 }), _jsx(Tooltip, { contentStyle: { background: "#0b1220", border: "1px solid #334155" }, formatter: (value, _name, p) => [
                            `${value} (${(p.payload.p * 100).toFixed(1)}%)`,
                            "shots",
                        ] }), _jsx(Bar, { dataKey: "count", fill: "#60a5fa" })] }) }) }));
}
