interface Curve {
  values: (number | null)[]; // length 101, 0-100% of stride
  color: string;
  strokeWidth: number;
  opacity?: number;
}

interface Props {
  title: string;
  curves: Curve[];
  height?: number;
}

const WIDTH = 600;

function pathFor(values: (number | null)[], lo: number, hi: number, height: number): string {
  const n = values.length;
  const xAt = (i: number) => (i / (n - 1)) * WIDTH;
  const yAt = (v: number) => height - ((v - lo) / (hi - lo)) * height;
  let path = "";
  let drawing = false;
  for (let i = 0; i < n; i++) {
    const v = values[i];
    if (v === null) {
      drawing = false;
      continue;
    }
    path += `${drawing ? "L" : "M"}${xAt(i).toFixed(1)},${yAt(v).toFixed(1)} `;
    drawing = true;
  }
  return path;
}

// X axis is % of stride cycle (0-100), not frame index or time — this is
// what makes two runners with different cadence comparable at all.
export function GaitCycleChart({ title, curves, height = 160 }: Props) {
  const allValues = curves.flatMap((c) => c.values).filter((v): v is number => v !== null);
  const min = allValues.length ? Math.min(...allValues) : 0;
  const max = allValues.length ? Math.max(...allValues) : 1;
  const pad = (max - min) * 0.1 || 1;
  const lo = min - pad;
  const hi = max + pad;

  return (
    <div>
      <div style={{ fontSize: 12, opacity: 0.8, marginBottom: 2 }}>{title}</div>
      <svg viewBox={`0 0 ${WIDTH} ${height}`} width="100%" height={height} style={{ background: "#111" }}>
        {curves.map((c, i) => (
          <path
            key={i}
            d={pathFor(c.values, lo, hi, height)}
            fill="none"
            stroke={c.color}
            strokeWidth={c.strokeWidth}
            opacity={c.opacity ?? 1}
          />
        ))}
      </svg>
      <div style={{ fontSize: 11, opacity: 0.6 }}>0% ────────── stride cycle ────────── 100%</div>
    </div>
  );
}
