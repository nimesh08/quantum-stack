import { useQuery } from "@tanstack/react-query";
import { api, Target } from "../api/jobs";

export interface TargetPickerProps {
  value: string;
  onChange: (id: string) => void;
}

export default function TargetPicker({ value, onChange }: TargetPickerProps) {
  const { data, isLoading } = useQuery({
    queryKey: ["targets"],
    queryFn: api.targets,
  });
  const targets: Target[] = data ?? [];
  return (
    <select
      data-testid="target-picker"
      value={value}
      onChange={(e) => onChange(e.target.value)}
      disabled={isLoading}
    >
      {targets.map((t) => (
        <option key={t.id} value={t.id}>
          {t.id} ({t.provider}, {t.qubits}q, ${t.pricing_per_shot_usd}/shot)
        </option>
      ))}
    </select>
  );
}
