import {
  L_ANKLE,
  L_FOOT_INDEX,
  L_HIP,
  L_KNEE,
  L_SHOULDER,
  R_ANKLE,
  R_FOOT_INDEX,
  R_HIP,
  R_KNEE,
  R_SHOULDER,
} from "./landmarkIndices";
import type { FrameLandmarks, JointAngles, Landmark } from "./types";

// SAGITTAL-PLANE ASSUMPTION: this whole module works in 2D image-space
// (x, y) only. z is deliberately ignored — MediaPipe's z estimate is a
// rough monocular guess and this rig assumes a fixed side-on camera, so
// only in-plane (sagittal) motion is meaningful. Do not "improve" these
// functions by incorporating z; that would be estimating depth we don't
// trust. See project constraints: sagittal view only.

// Angle at `b`, between vectors b->a and b->c, in degrees [0, 180].
function angleAt(a: Landmark, b: Landmark, c: Landmark): number {
  const v1x = a.x - b.x;
  const v1y = a.y - b.y;
  const v2x = c.x - b.x;
  const v2y = c.y - b.y;
  const dot = v1x * v2x + v1y * v2y;
  const mag1 = Math.hypot(v1x, v1y);
  const mag2 = Math.hypot(v2x, v2y);
  if (mag1 === 0 || mag2 === 0) return NaN;
  const cos = Math.max(-1, Math.min(1, dot / (mag1 * mag2)));
  return (Math.acos(cos) * 180) / Math.PI;
}

// Minimum visibility below which we treat a landmark as untrustworthy
// rather than compute a confident-looking angle from noise. Arbitrary
// threshold, not validated — tune by eye against the skeleton overlay.
const MIN_VISIBILITY = 0.5;

function usable(...landmarks: Landmark[]): boolean {
  return landmarks.every((lm) => lm.visibility >= MIN_VISIBILITY);
}

function angleOrNull(a: Landmark, b: Landmark, c: Landmark): number | null {
  if (!usable(a, b, c)) return null;
  const angle = angleAt(a, b, c);
  return Number.isNaN(angle) ? null : angle;
}

export function computeAngles(frame: FrameLandmarks): JointAngles {
  const lm = frame.landmarks;
  const base: JointAngles = {
    frameIndex: frame.frameIndex,
    timestampUs: frame.timestampUs,
    leftHip: null,
    rightHip: null,
    leftKnee: null,
    rightKnee: null,
    leftAnkle: null,
    rightAnkle: null,
    trunkLean: null,
    hipY: null,
  };
  if (!lm) return base;

  // Hip angle: trunk-thigh angle at the hip (shoulder-hip-knee).
  base.leftHip = angleOrNull(lm[L_SHOULDER], lm[L_HIP], lm[L_KNEE]);
  base.rightHip = angleOrNull(lm[R_SHOULDER], lm[R_HIP], lm[R_KNEE]);

  // Knee angle: thigh-shank angle at the knee (hip-knee-ankle).
  base.leftKnee = angleOrNull(lm[L_HIP], lm[L_KNEE], lm[L_ANKLE]);
  base.rightKnee = angleOrNull(lm[R_HIP], lm[R_KNEE], lm[R_ANKLE]);

  // Ankle angle: shank-foot angle at the ankle (knee-ankle-foot_index).
  base.leftAnkle = angleOrNull(lm[L_KNEE], lm[L_ANKLE], lm[L_FOOT_INDEX]);
  base.rightAnkle = angleOrNull(lm[R_KNEE], lm[R_ANKLE], lm[R_FOOT_INDEX]);

  // Trunk lean: angle of the hip->shoulder vector from vertical, signed.
  // Positive = shoulders forward of hips in image x (direction of travel
  // is not known, so this is a raw sagittal-plane angle, not "forward
  // lean" in a validated biomechanical sense).
  if (usable(lm[L_SHOULDER], lm[R_SHOULDER], lm[L_HIP], lm[R_HIP])) {
    const shoulderMidX = (lm[L_SHOULDER].x + lm[R_SHOULDER].x) / 2;
    const shoulderMidY = (lm[L_SHOULDER].y + lm[R_SHOULDER].y) / 2;
    const hipMidX = (lm[L_HIP].x + lm[R_HIP].x) / 2;
    const hipMidY = (lm[L_HIP].y + lm[R_HIP].y) / 2;
    const dx = shoulderMidX - hipMidX;
    const dy = shoulderMidY - hipMidY; // image y grows downward
    // atan2 against the "up" direction (-y) gives 0deg for a perfectly
    // upright trunk.
    base.trunkLean = (Math.atan2(dx, -dy) * 180) / Math.PI;
  }

  // Hip vertical position: normalized image y of the hip midpoint,
  // flipped so increasing values mean "higher" (up), for readability on
  // vertical-oscillation plots. Still in normalized image coordinates,
  // not real-world units — do not treat as centimeters.
  if (usable(lm[L_HIP], lm[R_HIP])) {
    const hipMidY = (lm[L_HIP].y + lm[R_HIP].y) / 2;
    base.hipY = 1 - hipMidY;
  }

  return base;
}

export function computeAngleSeries(frames: FrameLandmarks[]): JointAngles[] {
  return frames.map(computeAngles);
}
