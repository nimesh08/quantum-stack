import { useQuery } from "@tanstack/react-query";
import { api, Target } from "../api/jobs";

/** Props for {@link TargetPicker}. */
export interface TargetPickerProps {
  /** Currently selected chip id. */
  value: string;
  /** Called with the new chip id when the user picks one. */
  onChange: (id: string) => void;
}

/**
 * Dropdown of every chip the registry knows about, populated from
 * `GET /api/v1/targets` via TanStack Query. Disabled while loading.
 */
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
