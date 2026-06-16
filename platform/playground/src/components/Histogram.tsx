import { Bar, BarChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";

/** Props for {@link Histogram}. */
export interface HistogramProps {
  /** Bitstring → shot-count map. */
  counts: Record<string, number>;
  /** Total shot count (used to compute probabilities in tooltips). */
  shots: number;
}

/**
 * Bar chart of measurement outcomes. Bars are sorted by count
 * descending. The tooltip shows raw count + probability.
 *
 * @example
 * ```tsx
 * <Histogram counts={{ "00": 498, "11": 502 }} shots={1000} />
 * ```
 */
export default function Histogram({ counts, shots }: HistogramProps) {
  const data = Object.entries(counts)
    .map(([k, v]) => ({ outcome: k, count: v, p: shots > 0 ? v / shots : 0 }))
    .sort((a, b) => b.count - a.count);
  return (
    <div className="histogram" data-testid="histogram">
      <ResponsiveContainer width="100%" height="100%">
        <BarChart data={data} margin={{ left: 10, right: 10, top: 10, bottom: 10 }}>
          <XAxis dataKey="outcome" stroke="#94a3b8" fontSize={12} />
          <YAxis stroke="#94a3b8" fontSize={12} />
          <Tooltip
            contentStyle={{ background: "#0b1220", border: "1px solid #334155" }}
            formatter={(value: number, _name: string, p: any) => [
              `${value} (${(p.payload.p * 100).toFixed(1)}%)`,
              "shots",
            ]}
          />
          <Bar dataKey="count" fill="#60a5fa" />
        </BarChart>
      </ResponsiveContainer>
    </div>
  );
}
