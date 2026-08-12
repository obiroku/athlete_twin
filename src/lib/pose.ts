import { PoseLandmarker } from "@mediapipe/tasks-vision";
import wasmLoaderUrl from "@mediapipe/tasks-vision/vision_wasm_internal.js?url";
import wasmBinaryUrl from "@mediapipe/tasks-vision/vision_wasm_internal.wasm?url";
import type { DecodedFrame, FrameLandmarks, Landmark } from "./types";

// The wasm engine is bundled as a local asset (via Vite's ?url imports)
// rather than fetched from Google's CDN, so pose tracking works offline
// after the initial page load — consistent with local-only processing.
// This skips SIMD-capability detection and always loads the SIMD build;
// virtually all browsers that can run WebCodecs also support wasm SIMD,
// and falling back gracefully isn't worth the complexity here.
//
// The .task model weights are NOT bundled (they're tens of MB) and are
// fetched from Google's model CDN on first use. That fetch has nothing to
// do with the athlete's video — no frame or landmark data is ever sent
// anywhere. Video and derived pose data stay on-device.
const POSE_MODEL_URL =
  "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_heavy/float16/latest/pose_landmarker_heavy.task";

let landmarkerPromise: Promise<PoseLandmarker> | null = null;

function getLandmarker(): Promise<PoseLandmarker> {
  if (!landmarkerPromise) {
    landmarkerPromise = PoseLandmarker.createFromOptions(
      { wasmLoaderPath: wasmLoaderUrl, wasmBinaryPath: wasmBinaryUrl },
      {
        // CPU delegate: slower than GPU, but GPU has known compatibility
        // gaps (Safari/WebGL context setup) and this phase cares more
        // about "does tracking work at all" than throughput.
        baseOptions: { modelAssetPath: POSE_MODEL_URL, delegate: "CPU" },
        runningMode: "VIDEO",
        numPoses: 1,
      },
    );
  }
  return landmarkerPromise;
}

export interface TrackProgress {
  processed: number;
  total: number;
}

// Runs PoseLandmarker on every decoded frame, in order. `detectForVideo`
// requires monotonically increasing timestamps in the same running-mode
// session, so frames must be fed sequentially — this is not parallelized.
export async function trackFrames(
  frames: DecodedFrame[],
  onProgress?: (p: TrackProgress) => void,
): Promise<FrameLandmarks[]> {
  const landmarker = await getLandmarker();
  const results: FrameLandmarks[] = [];

  for (const frame of frames) {
    const timestampMs = Math.round(frame.timestampUs / 1000);
    const result = landmarker.detectForVideo(frame.bitmap, timestampMs);
    const raw = result.landmarks[0];
    const landmarks: Landmark[] | null = raw
      ? raw.map((lm) => ({ x: lm.x, y: lm.y, z: lm.z, visibility: lm.visibility }))
      : null;
    results.push({ frameIndex: frame.index, timestampUs: frame.timestampUs, landmarks });
    onProgress?.({ processed: results.length, total: frames.length });
  }

  return results;
}
