import type { AthleteSession } from "./types";

function download(filename: string, contents: string) {
  const blob = new Blob([contents], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export function exportLandmarks(session: AthleteSession) {
  const payload = {
    fileName: session.fileName,
    frameRate: session.frameRate,
    landmarks: session.landmarks,
  };
  download(`${session.label || "athlete"}-landmarks.json`, JSON.stringify(payload, null, 2));
}

export function exportLabels(session: AthleteSession) {
  const payload = {
    fileName: session.fileName,
    frameRate: session.frameRate,
    manualLabels: session.manualLabels,
    heuristicEvents: session.heuristicEvents,
  };
  download(`${session.label || "athlete"}-labels.json`, JSON.stringify(payload, null, 2));
}
