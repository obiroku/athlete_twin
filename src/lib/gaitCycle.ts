import type { GaitLabel, JointAngles } from "./types";

// Normalizes stride cycles onto a common 0-100% axis so two athletes (or
// two strides from the same athlete) can be overlaid despite different
// cadence and different absolute frame counts per stride. This is a
// straightforward resample, not a validated gait model.

export interface Cycle {
  startFrameIndex: number;
  endFrameIndex: number;
}

// Cycle boundaries come from same-side footstrike-to-footstrike pairs in
// human labels — the only thing this rig treats as ground truth. Sorted by
// frame index; assumes labels are for a single side at a time.
export function cyclesFromFootstrikes(labels: GaitLabel[], side: GaitLabel["side"]): Cycle[] {
  const strikes = labels
    .filter((l) => l.type === "footstrike" && l.side === side)
    .sort((a, b) => a.frameIndex - b.frameIndex);

  const cycles: Cycle[] = [];
  for (let i = 0; i < strikes.length - 1; i++) {
    cycles.push({ startFrameIndex: strikes[i].frameIndex, endFrameIndex: strikes[i + 1].frameIndex });
  }
  return cycles;
}

const NORMALIZED_POINTS = 101; // 0%, 1%, ..., 100%

// Resamples one field of the angle series over a cycle onto 101 evenly
// spaced points via linear interpolation. Returns null at points where the
// surrounding source samples are missing (angle computation failed).
export function normalizeCycle(
  angles: JointAngles[],
  cycle: Cycle,
  field: keyof Omit<JointAngles, "frameIndex" | "timestampUs">,
): (number | null)[] {
  const slice = angles.filter(
    (a) => a.frameIndex >= cycle.startFrameIndex && a.frameIndex <= cycle.endFrameIndex,
  );
  if (slice.length < 2) return new Array(NORMALIZED_POINTS).fill(null);

  const span = cycle.endFrameIndex - cycle.startFrameIndex;
  const result: (number | null)[] = [];

  for (let p = 0; p < NORMALIZED_POINTS; p++) {
    const targetFrame = cycle.startFrameIndex + (span * p) / (NORMALIZED_POINTS - 1);

    // Find the two source samples bracketing targetFrame.
    let lo = slice[0];
    let hi = slice[slice.length - 1];
    for (let i = 0; i < slice.length - 1; i++) {
      if (slice[i].frameIndex <= targetFrame && slice[i + 1].frameIndex >= targetFrame) {
        lo = slice[i];
        hi = slice[i + 1];
        break;
      }
    }
    const loVal = lo[field] as number | null;
    const hiVal = hi[field] as number | null;
    if (loVal === null || hiVal === null) {
      result.push(null);
      continue;
    }
    if (hi.frameIndex === lo.frameIndex) {
      result.push(loVal);
      continue;
    }
    const t = (targetFrame - lo.frameIndex) / (hi.frameIndex - lo.frameIndex);
    result.push(loVal + (hiVal - loVal) * t);
  }

  return result;
}

// Elementwise mean across cycles, ignoring nulls at each point
// independently so one bad cycle doesn't null out the whole average.
export function averageCurves(curves: (number | null)[][]): (number | null)[] {
  if (curves.length === 0) return [];
  const length = curves[0].length;
  const result: (number | null)[] = [];
  for (let i = 0; i < length; i++) {
    const values = curves.map((c) => c[i]).filter((v): v is number => v !== null);
    result.push(values.length > 0 ? values.reduce((a, b) => a + b, 0) / values.length : null);
  }
  return result;
}
