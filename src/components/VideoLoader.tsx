import { useState } from "react";
import { buildSession, type PipelineProgress } from "../lib/session";
import type { AthleteSession } from "../lib/types";

interface Props {
  label: string;
  onLoaded: (session: AthleteSession) => void;
}

function progressText(p: PipelineProgress): string {
  if (p.stage === "decoding") return `decoding frames: ${p.decode?.decoded ?? 0}/${p.decode?.total ?? "?"}`;
  if (p.stage === "tracking") return `running pose tracking: ${p.track?.processed ?? 0}/${p.track?.total ?? "?"}`;
  return "done";
}

export function VideoLoader({ label, onLoaded }: Props) {
  const [progress, setProgress] = useState<PipelineProgress | null>(null);
  const [error, setError] = useState<string | null>(null);

  const handleFile = async (file: File) => {
    setError(null);
    setProgress({ stage: "decoding" });
    try {
      const session = await buildSession(crypto.randomUUID(), label, file, setProgress);
      onLoaded(session);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setProgress(null);
    }
  };

  return (
    <div>
      <input
        type="file"
        accept="video/*"
        onChange={(e) => {
          const file = e.target.files?.[0];
          if (file) void handleFile(file);
        }}
      />
      {progress && <div style={{ marginTop: 6 }}>{progressText(progress)}</div>}
      {error && <div style={{ marginTop: 6, color: "#ff8a80" }}>error: {error}</div>}
    </div>
  );
}
