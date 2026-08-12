// Shared types for the Milestone 0 diagnostic rig.
// This is disposable research code — types will change as we learn.

export interface Landmark {
  x: number;
  y: number;
  z: number;
  visibility: number;
}

// One decoded video frame, kept in memory for scrubbing/overlay.
// `timestampUs` is the true presentation timestamp derived during decode,
// never a value inferred from a fixed fps assumption.
export interface DecodedFrame {
  index: number;
  timestampUs: number;
  bitmap: ImageBitmap;
}

export interface FrameLandmarks {
  frameIndex: number;
  timestampUs: number;
  landmarks: Landmark[] | null; // null = pose tracking failed on this frame
}

export interface JointAngles {
  frameIndex: number;
  timestampUs: number;
  leftHip: number | null;
  rightHip: number | null;
  leftKnee: number | null;
  rightKnee: number | null;
  leftAnkle: number | null;
  rightAnkle: number | null;
  trunkLean: number | null;
  hipY: number | null;
}

export type GaitEventType = "footstrike" | "toeOff";
export type GaitSide = "left" | "right";

export interface GaitLabel {
  frameIndex: number;
  timestampUs: number;
  type: GaitEventType;
  side: GaitSide;
}

// Output of the unvalidated heuristic detector. Kept separate from human
// labels so the two can be visually compared — never treated as ground truth.
export interface HeuristicEvent {
  frameIndex: number;
  timestampUs: number;
  type: GaitEventType;
  side: GaitSide;
  confidence: number; // arbitrary 0-1 score from the heuristic, not calibrated
}

export interface FrameRateReport {
  effectiveFps: number;
  medianDeltaUs: number;
  sampleCount: number;
  // True if timestamp deltas were inconsistent enough that "effective fps"
  // is a shaky single number (e.g. variable frame rate, dropped frames).
  jittery: boolean;
}

export interface AthleteSession {
  id: string;
  label: string;
  fileName: string;
  frames: DecodedFrame[];
  landmarks: FrameLandmarks[];
  angles: JointAngles[];
  frameRate: FrameRateReport;
  manualLabels: GaitLabel[];
  heuristicEvents: HeuristicEvent[];
}
