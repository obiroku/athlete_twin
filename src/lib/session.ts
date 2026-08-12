import { computeAngleSeries } from "./angles";
import { detectHeuristicEvents } from "./heuristicEvents";
import type { AthleteSession, FrameRateReport } from "./types";
import { trackFrames, type TrackProgress } from "./pose";
import { extractFrames, type ExtractProgress } from "./videoDecode";

export type PipelineStage = "decoding" | "tracking" | "done";

export interface PipelineProgress {
  stage: PipelineStage;
  decode?: ExtractProgress;
  track?: TrackProgress;
}

// Heuristic detector window: half of a 200ms rough ground-contact target,
// converted to a frame count from the *measured* effective fps — never a
// hardcoded fps assumption. Clamped so it's never zero at low frame rates.
function windowFramesFor(frameRate: FrameRateReport): number {
  const targetSeconds = 0.1;
  return Math.max(1, Math.round(frameRate.effectiveFps * targetSeconds));
}

export async function buildSession(
  id: string,
  label: string,
  file: File,
  onProgress?: (p: PipelineProgress) => void,
): Promise<AthleteSession> {
  const { frames, frameRate } = await extractFrames(file, (decode) =>
    onProgress?.({ stage: "decoding", decode }),
  );

  const landmarks = await trackFrames(frames, (track) => onProgress?.({ stage: "tracking", track }));

  const angles = computeAngleSeries(landmarks);
  const heuristicEvents = detectHeuristicEvents(landmarks, windowFramesFor(frameRate));

  onProgress?.({ stage: "done" });

  return {
    id,
    label,
    fileName: file.name,
    frames,
    landmarks,
    angles,
    frameRate,
    manualLabels: [],
    heuristicEvents,
  };
}
