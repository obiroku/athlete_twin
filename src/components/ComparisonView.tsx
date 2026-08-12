import { useState } from "react";
import { GaitCycleChart } from "./GaitCycleChart";
import { averageCurves, cyclesFromFootstrikes, normalizeCycle } from "../lib/gaitCycle";
import type { AthleteSession, GaitSide, JointAngles } from "../lib/types";

interface Props {
  sessionA: AthleteSession;
  sessionB: AthleteSession;
}

const FIELDS: { field: keyof Omit<JointAngles, "frameIndex" | "timestampUs">; title: string }[] = [
  { field: "leftHip", title: "hip angle (deg)" },
  { field: "leftKnee", title: "knee angle (deg)" },
  { field: "leftAnkle", title: "ankle angle (deg)" },
  { field: "trunkLean", title: "trunk lean (deg from vertical)" },
  { field: "hipY", title: "hip vertical position (normalized)" },
];

function fieldForSide(
  base: keyof Omit<JointAngles, "frameIndex" | "timestampUs">,
  side: GaitSide,
): keyof Omit<JointAngles, "frameIndex" | "timestampUs"> {
  if (side === "right" && base.startsWith("left")) {
    return ("right" + base.slice(4)) as typeof base;
  }
  return base;
}

// Comparison requires manual footstrike labels — this rig treats the
// heuristic detector as too unreliable to define cycle boundaries for a
// cross-athlete comparison. If there aren't at least two same-side
// footstrikes, there's no cycle to normalize.
export function ComparisonView({ sessionA, sessionB }: Props) {
  const [side, setSide] = useState<GaitSide>("left");

  const cyclesA = cyclesFromFootstrikes(sessionA.manualLabels, side);
  const cyclesB = cyclesFromFootstrikes(sessionB.manualLabels, side);

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 12 }}>
      <div style={{ display: "flex", gap: 12, alignItems: "center" }}>
        <strong>Comparing:</strong>
        <span style={{ color: "#4fc3f7" }}>{sessionA.label}</span> vs{" "}
        <span style={{ color: "#f06292" }}>{sessionB.label}</span>
        <label>
          side:{" "}
          <select value={side} onChange={(e) => setSide(e.target.value as GaitSide)}>
            <option value="left">left</option>
            <option value="right">right</option>
          </select>
        </label>
      </div>

      {(cyclesA.length === 0 || cyclesB.length === 0) && (
        <div style={{ color: "#ff8a80" }}>
          Need at least two {side} footstrike labels on both athletes to normalize a cycle. {sessionA.label}:{" "}
          {cyclesA.length} cycle(s), {sessionB.label}: {cyclesB.length} cycle(s).
        </div>
      )}

      {FIELDS.map(({ field, title }) => {
        const f = fieldForSide(field, side);
        const curvesA = cyclesA.map((c) => normalizeCycle(sessionA.angles, c, f));
        const curvesB = cyclesB.map((c) => normalizeCycle(sessionB.angles, c, f));
        const avgA = averageCurves(curvesA);
        const avgB = averageCurves(curvesB);

        return (
          <GaitCycleChart
            key={field}
            title={`${title} — ${side}`}
            curves={[
              ...curvesA.map((c) => ({ values: c, color: "#4fc3f7", strokeWidth: 1, opacity: 0.25 })),
              ...curvesB.map((c) => ({ values: c, color: "#f06292", strokeWidth: 1, opacity: 0.25 })),
              { values: avgA, color: "#4fc3f7", strokeWidth: 2.5 },
              { values: avgB, color: "#f06292", strokeWidth: 2.5 },
            ]}
          />
        );
      })}
    </div>
  );
}
