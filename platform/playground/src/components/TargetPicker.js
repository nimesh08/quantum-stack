import { jsxs as _jsxs, jsx as _jsx } from "react/jsx-runtime";
import { useQuery } from "@tanstack/react-query";
import { api } from "../api/jobs";
export default function TargetPicker({ value, onChange }) {
    const { data, isLoading } = useQuery({
        queryKey: ["targets"],
        queryFn: api.targets,
    });
    const targets = data ?? [];
    return (_jsx("select", { "data-testid": "target-picker", value: value, onChange: (e) => onChange(e.target.value), disabled: isLoading, children: targets.map((t) => (_jsxs("option", { value: t.id, children: [t.id, " (", t.provider, ", ", t.qubits, "q, $", t.pricing_per_shot_usd, "/shot)"] }, t.id))) }));
}
