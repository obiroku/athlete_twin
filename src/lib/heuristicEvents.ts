import { L_ANKLE, R_ANKLE } from "./landmarkIndices";
import type { FrameLandmarks, GaitSide, HeuristicEvent } from "./types";

// UNVALIDATED. Do not read this as ground truth or describe it as accurate,
// robust, or validated anywhere it's surfaced in the UI. It exists so a
// human reviewer has a rough starting point to compare against manual
// labels — see Milestone 0's biomechanics assumption: ground truth comes
// from human review, not from this detector.
//
// Heuristic: for a side-on runner, image-space ankle y reaches a local
// maximum (foot at its lowest point on screen) around footstrike, and a
// local minimum shortly after (foot lifting) around toe-off. This ignores
// everything about actual ground contact, foot-camera distance, and
// perspective — it's the simplest defensible signal, not a physical model.

const MIN_VISIBILITY = 0.5;

interface AnkleSample {
  frameIndex: number;
  timestampUs: number;
  y: number | null;
}

function ankleSamplesForSide(frames: FrameLandmarks[], ankleIndex: number): AnkleSample[] {
  return frames.map((f) => {
    const lm = f.landmarks?.[ankleIndex];
    const y = lm && lm.visibility >= MIN_VISIBILITY ? lm.y : null;
    return { frameIndex: f.frameIndex, timestampUs: f.timestampUs, y };
  });
}

function localExtremaIndices(
  values: (number | null)[],
  window: number,
  findMin: boolean,
): number[] {
  const indices: number[] = [];
  for (let i = 0; i < values.length; i++) {
    const v = values[i];
    if (v === null) continue;
    let isExtreme = true;
    for (let j = Math.max(0, i - window); j <= Math.min(values.length - 1, i + window); j++) {
      if (j === i) continue;
      const other = values[j];
      if (other === null) continue;
      if (findMin ? other < v : other > v) {
        isExtreme = false;
        break;
      }
    }
    if (isExtreme) indices.push(i);
  }
  return indices;
}

function detectForSide(samples: AnkleSample[], side: GaitSide, window: number): HeuristicEvent[] {
  const ys = samples.map((s) => s.y);
  // Footstrike: ankle y at a local max (image y grows downward, so this is
  // the foot's lowest point on screen).
  const footstrikeIdx = localExtremaIndices(ys, window, false);
  // Toe-off: ankle y at a local min (foot lifting off).
  const toeOffIdx = localExtremaIndices(ys, window, true);

  const events: HeuristicEvent[] = [
    ...footstrikeIdx.map((i) => ({
      frameIndex: samples[i].frameIndex,
      timestampUs: samples[i].timestampUs,
      type: "footstrike" as const,
      side,
      confidence: 0.5, // arbitrary, not calibrated
    })),
    ...toeOffIdx.map((i) => ({
      frameIndex: samples[i].frameIndex,
      timestampUs: samples[i].timestampUs,
      type: "toeOff" as const,
      side,
      confidence: 0.5,
    })),
  ];
  return events.sort((a, b) => a.frameIndex - b.frameIndex);
}

// `window` is a frame-count radius for local-extrema detection, not a time
// duration. Callers should scale it with effective fps so it roughly spans
// a fraction of expected ground-contact time (~180-250ms) — see the fps
// warning in the frame-rate report before trusting any of this.
export function detectHeuristicEvents(
  frames: FrameLandmarks[],
  window: number,
): HeuristicEvent[] {
  const left = detectForSide(ankleSamplesForSide(frames, L_ANKLE), "left", window);
  const right = detectForSide(ankleSamplesForSide(frames, R_ANKLE), "right", window);
  return [...left, ...right].sort((a, b) => a.frameIndex - b.frameIndex);
}
