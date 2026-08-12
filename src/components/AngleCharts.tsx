import { LineChart } from "./LineChart";
import type { GaitLabel, HeuristicEvent, JointAngles } from "../lib/types";

interface Props {
  angles: JointAngles[];
  currentIndex: number;
  onSeek: (index: number) => void;
  manualLabels: GaitLabel[];
  heuristicEvents: HeuristicEvent[];
}

const MANUAL_COLORS = { footstrike: "#4caf50", toeOff: "#ff9800" };
const HEURISTIC_COLORS = { footstrike: "#1b5e20", toeOff: "#e65100" };

function eventMarkers(manualLabels: GaitLabel[], heuristicEvents: HeuristicEvent[]) {
  return [
    ...manualLabels.map((l) => ({ frameIndex: l.frameIndex, color: MANUAL_COLORS[l.type] })),
    ...heuristicEvents.map((e) => ({ frameIndex: e.frameIndex, color: HEURISTIC_COLORS[e.type] })),
  ];
}

// Solid, saturated lines = human labels. Dark/muted lines = the
// unvalidated heuristic detector. Never collapse the two into one color —
// the whole point is being able to see where they disagree.
export function AngleCharts({ angles, currentIndex, onSeek, manualLabels, heuristicEvents }: Props) {
  const markers = eventMarkers(manualLabels, heuristicEvents);

  const series: { title: string; field: keyof JointAngles; color: string }[] = [
    { title: "left hip angle (deg)", field: "leftHip", color: "#4fc3f7" },
    { title: "right hip angle (deg)", field: "rightHip", color: "#81d4fa" },
    { title: "left knee angle (deg)", field: "leftKnee", color: "#f06292" },
    { title: "right knee angle (deg)", field: "rightKnee", color: "#f8bbd0" },
    { title: "left ankle angle (deg)", field: "leftAnkle", color: "#ba68c8" },
    { title: "right ankle angle (deg)", field: "rightAnkle", color: "#ce93d8" },
    { title: "trunk lean (deg from vertical)", field: "trunkLean", color: "#ffd54f" },
    { title: "hip vertical position (normalized)", field: "hipY", color: "#a5d6a7" },
  ];

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
      {series.map((s) => (
        <LineChart
          key={s.field}
          title={s.title}
          values={angles.map((a) => a[s.field] as number | null)}
          currentIndex={currentIndex}
          onSeek={onSeek}
          color={s.color}
          events={markers}
        />
      ))}
    </div>
  );
}
