interface Props {
  index: number;
  total: number;
  timestampUs: number;
  onChange: (index: number) => void;
}

export function FrameStepper({ index, total, timestampUs, onChange }: Props) {
  const clamp = (i: number) => Math.max(0, Math.min(total - 1, i));

  return (
    <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
      <button onClick={() => onChange(clamp(index - 1))} disabled={index <= 0}>
        ◀ prev
      </button>
      <input
        type="range"
        min={0}
        max={Math.max(0, total - 1)}
        value={index}
        onChange={(e) => onChange(clamp(Number(e.target.value)))}
        style={{ flex: 1 }}
      />
      <button onClick={() => onChange(clamp(index + 1))} disabled={index >= total - 1}>
        next ▶
      </button>
      <span style={{ minWidth: 160, textAlign: "right" }}>
        frame {index + 1}/{total} · {(timestampUs / 1000).toFixed(1)}ms
      </span>
    </div>
  );
}
