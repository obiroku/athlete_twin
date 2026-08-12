import { useEffect, useRef } from "react";
import { PoseLandmarker } from "@mediapipe/tasks-vision";
import type { DecodedFrame, Landmark } from "../lib/types";

const MIN_VISIBILITY = 0.5;

interface Props {
  frame: DecodedFrame | undefined;
  landmarks: Landmark[] | null;
}

export function SkeletonOverlay({ frame, landmarks }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !frame) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    canvas.width = frame.bitmap.width;
    canvas.height = frame.bitmap.height;
    ctx.drawImage(frame.bitmap, 0, 0);

    if (!landmarks) return;

    ctx.strokeStyle = "#00e5ff";
    ctx.lineWidth = Math.max(1, canvas.width / 300);
    for (const { start, end } of PoseLandmarker.POSE_CONNECTIONS) {
      const a = landmarks[start];
      const b = landmarks[end];
      if (!a || !b) continue;
      if (a.visibility < MIN_VISIBILITY || b.visibility < MIN_VISIBILITY) continue;
      ctx.beginPath();
      ctx.moveTo(a.x * canvas.width, a.y * canvas.height);
      ctx.lineTo(b.x * canvas.width, b.y * canvas.height);
      ctx.stroke();
    }

    ctx.fillStyle = "#ff3d81";
    const r = Math.max(2, canvas.width / 200);
    for (const lm of landmarks) {
      if (lm.visibility < MIN_VISIBILITY) continue;
      ctx.beginPath();
      ctx.arc(lm.x * canvas.width, lm.y * canvas.height, r, 0, Math.PI * 2);
      ctx.fill();
    }
  }, [frame, landmarks]);

  return <canvas ref={canvasRef} style={{ width: "100%", maxWidth: 480, background: "#000" }} />;
}
