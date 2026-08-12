# Athlete Twin — Milestone 0 diagnostic rig

Disposable research code. This is not the product — it's a rig to answer
one question:

> Does side-on phone video contain stable, athlete-specific movement
> signatures clean enough to support future classification?

If the answer turns out to be no, the correct outcome is to stop or
rethink the product, not push forward into building features on top of
this. Nothing here should be treated as production architecture.

## What it does

Load a local side-on running video (iPhone slow-motion clips are the
target case) and the app will:

1. Decode every frame via WebCodecs, deriving the *true* effective frame
   rate from decoded timestamps — never from container metadata, since
   iPhone slow-mo files routinely lie about this by 4-8x.
2. Run MediaPipe PoseLandmarker (heavy model) on every frame, on-device.
3. Compute hip/knee/ankle angle, trunk lean, and hip vertical position
   per frame (2D, sagittal-plane only).
4. Let you step frame-by-frame with a skeleton overlay, inspect the raw
   angle signals, and manually label footstrike/toe-off events — manual
   labels are the only thing this rig treats as ground truth.
5. Run a clearly-unvalidated heuristic footstrike/toe-off detector
   alongside the manual labels, purely so a human reviewer has something
   to eyeball against.
6. Export landmarks and labels as JSON.
7. Normalize two athletes' strides onto a common 0-100% cycle axis and
   overlay them for comparison.

All processing is on-device. Video never leaves the browser. The only
network request this app makes is fetching the ~30MB PoseLandmarker
model weights from Google's model CDN on first use — no video or
landmark data is ever sent anywhere.

## Judging Milestone 0

Success looks like: pose tracking stays stable on most frames, angle
curves are smooth enough to eyeball stride cycles, heuristic detections
roughly track human labels, and two different runners produce visibly
different normalized curves.

Failure looks like: landmarks drop out constantly, timestamps are
unreliable, angle signals are mostly noise, or runners look
indistinguishable after normalization. If that's what you see, stop and
rethink rather than keep building on this.

## Running it

```bash
npm install
npm run dev
```

Load a video, watch the effective-fps badge — if it's red (below
60fps), gait events at typical ~180-250ms ground contact are only
spanning a handful of frames and event timing should not be trusted.

## Stack

Vite + React + TypeScript, `@mediapipe/tasks-vision` (WASM), WebCodecs
for frame extraction, `mp4box` for demuxing (WebCodecs needs decoded
samples, not a container). No router, state library, CSS framework, or
test framework — this is a research tool, not a product.

## Working style

Small, single-concern commits. Plain functions over abstractions. Large
parts of this are expected to be rewritten or thrown away once
Milestone 0 has an answer.
