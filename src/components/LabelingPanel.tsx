import type { GaitEventType, GaitLabel, GaitSide } from "../lib/types";

interface Props {
  currentIndex: number;
  currentTimestampUs: number;
  labels: GaitLabel[];
  onAdd: (label: GaitLabel) => void;
  onRemove: (frameIndex: number, type: GaitEventType, side: GaitSide) => void;
  onExportLabels: () => void;
  onExportLandmarks: () => void;
}

// Manual labeling is ground truth for this rig — everything else
// (heuristic detector, later classification) is judged against it, not
// the other way around.
export function LabelingPanel({
  currentIndex,
  currentTimestampUs,
  labels,
  onAdd,
  onRemove,
  onExportLabels,
  onExportLandmarks,
}: Props) {
  const addLabel = (type: GaitEventType, side: GaitSide) => {
    onAdd({ frameIndex: currentIndex, timestampUs: currentTimestampUs, type, side });
  };

  const sorted = [...labels].sort((a, b) => a.frameIndex - b.frameIndex);

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
      <div style={{ display: "flex", gap: 6, flexWrap: "wrap" }}>
        <button onClick={() => addLabel("footstrike", "left")}>+ left footstrike</button>
        <button onClick={() => addLabel("footstrike", "right")}>+ right footstrike</button>
        <button onClick={() => addLabel("toeOff", "left")}>+ left toe-off</button>
        <button onClick={() => addLabel("toeOff", "right")}>+ right toe-off</button>
      </div>
      <div style={{ display: "flex", gap: 6 }}>
        <button onClick={onExportLabels}>export labels JSON</button>
        <button onClick={onExportLandmarks}>export landmarks JSON</button>
      </div>
      <div style={{ maxHeight: 160, overflowY: "auto", fontSize: 12 }}>
        {sorted.length === 0 && <div style={{ opacity: 0.6 }}>no labels yet</div>}
        {sorted.map((l) => (
          <div
            key={`${l.frameIndex}-${l.type}-${l.side}`}
            style={{ display: "flex", gap: 8, alignItems: "center" }}
          >
            <span>
              frame {l.frameIndex} · {l.side} {l.type}
            </span>
            <button onClick={() => onRemove(l.frameIndex, l.type, l.side)}>x</button>
          </div>
        ))}
      </div>
    </div>
  );
}
