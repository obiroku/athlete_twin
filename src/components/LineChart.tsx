interface EventMarker {
  frameIndex: number;
  color: string;
  label?: string;
}

interface Props {
  title: string;
  values: (number | null)[];
  currentIndex: number;
  onSeek?: (index: number) => void;
  color?: string;
  height?: number;
  events?: EventMarker[];
}

const WIDTH = 800;

// Hand-rolled SVG line chart — this is a research tool, not a product UI,
// and a plotting library would be more ceremony than the job needs.
export function LineChart({
  title,
  values,
  currentIndex,
  onSeek,
  color = "#4fc3f7",
  height = 120,
  events = [],
}: Props) {
  const n = values.length;
  const numeric = values.filter((v): v is number => v !== null);
  const min = numeric.length ? Math.min(...numeric) : 0;
  const max = numeric.length ? Math.max(...numeric) : 1;
  const pad = (max - min) * 0.1 || 1;
  const lo = min - pad;
  const hi = max + pad;

  const xAt = (i: number) => (n <= 1 ? 0 : (i / (n - 1)) * WIDTH);
  const yAt = (v: number) => height - ((v - lo) / (hi - lo)) * height;

  let path = "";
  let drawing = false;
  for (let i = 0; i < n; i++) {
    const v = values[i];
    if (v === null) {
      drawing = false;
      continue;
    }
    const cmd = drawing ? "L" : "M";
    path += `${cmd}${xAt(i).toFixed(1)},${yAt(v).toFixed(1)} `;
    drawing = true;
  }

  const handleClick: React.MouseEventHandler<SVGSVGElement> = (e) => {
    if (!onSeek || n === 0) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const frac = (e.clientX - rect.left) / rect.width;
    onSeek(Math.max(0, Math.min(n - 1, Math.round(frac * (n - 1)))));
  };

  return (
    <div>
      <div style={{ fontSize: 12, opacity: 0.8, marginBottom: 2 }}>
        {title}
        {numeric.length === 0 && " — no data (tracking failed on every frame)"}
      </div>
      <svg
        viewBox={`0 0 ${WIDTH} ${height}`}
        width="100%"
        height={height}
        onClick={handleClick}
        style={{ background: "#111", cursor: onSeek ? "pointer" : "default", display: "block" }}
      >
        <path d={path} fill="none" stroke={color} strokeWidth={1.5} />
        {events.map((ev, i) => (
          <line
            key={i}
            x1={xAt(ev.frameIndex)}
            x2={xAt(ev.frameIndex)}
            y1={0}
            y2={height}
            stroke={ev.color}
            strokeWidth={1}
            opacity={0.6}
          />
        ))}
        <line
          x1={xAt(currentIndex)}
          x2={xAt(currentIndex)}
          y1={0}
          y2={height}
          stroke="#fff"
          strokeWidth={1}
        />
      </svg>
    </div>
  );
}
