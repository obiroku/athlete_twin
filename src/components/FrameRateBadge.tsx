import type { FrameRateReport } from "../lib/types";

// Frame rate is the highest-risk part of this rig — surface it prominently
// and warn loudly rather than let a bad estimate hide in a corner.
export function FrameRateBadge({ frameRate }: { frameRate: FrameRateReport }) {
  const low = frameRate.effectiveFps < 60;
  return (
    <div
      style={{
        padding: "6px 10px",
        border: `1px solid ${low ? "#e53935" : "#4caf50"}`,
        color: low ? "#ff8a80" : "#a5d6a7",
        display: "inline-block",
        fontSize: 13,
      }}
    >
      effective fps: {frameRate.effectiveFps.toFixed(1)}
      {frameRate.jittery && " · JITTERY (inconsistent frame spacing)"}
      {low && " · WARNING: below 60fps, gait events (~180-250ms contact) may span too few frames"}
    </div>
  );
}
