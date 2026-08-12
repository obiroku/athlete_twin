import { useState } from "react";
import { AngleCharts } from "./AngleCharts";
import { FrameRateBadge } from "./FrameRateBadge";
import { FrameStepper } from "./FrameStepper";
import { LabelingPanel } from "./LabelingPanel";
import { SkeletonOverlay } from "./SkeletonOverlay";
import { exportLabels, exportLandmarks } from "../lib/exportData";
import type { AthleteSession, GaitEventType, GaitLabel, GaitSide } from "../lib/types";

interface Props {
  session: AthleteSession;
  onChange: (session: AthleteSession) => void;
}

export function AthleteWorkspace({ session, onChange }: Props) {
  const [index, setIndex] = useState(0);

  const frame = session.frames[index];
  const landmarks = session.landmarks[index]?.landmarks ?? null;

  const addLabel = (label: GaitLabel) => {
    const withoutDup = session.manualLabels.filter(
      (l) => !(l.frameIndex === label.frameIndex && l.type === label.type && l.side === label.side),
    );
    onChange({ ...session, manualLabels: [...withoutDup, label] });
  };

  const removeLabel = (frameIndex: number, type: GaitEventType, side: GaitSide) => {
    onChange({
      ...session,
      manualLabels: session.manualLabels.filter(
        (l) => !(l.frameIndex === frameIndex && l.type === type && l.side === side),
      ),
    });
  };

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 12 }}>
      <div style={{ display: "flex", gap: 8, alignItems: "center" }}>
        <strong>{session.label}</strong>
        <span style={{ opacity: 0.7 }}>{session.fileName}</span>
        <FrameRateBadge frameRate={session.frameRate} />
      </div>

      <div style={{ display: "flex", gap: 16, flexWrap: "wrap" }}>
        <SkeletonOverlay frame={frame} landmarks={landmarks} />
        <div style={{ flex: 1, minWidth: 280 }}>
          <LabelingPanel
            currentIndex={index}
            currentTimestampUs={frame?.timestampUs ?? 0}
            labels={session.manualLabels}
            onAdd={addLabel}
            onRemove={removeLabel}
            onExportLabels={() => exportLabels(session)}
            onExportLandmarks={() => exportLandmarks(session)}
          />
        </div>
      </div>

      <FrameStepper
        index={index}
        total={session.frames.length}
        timestampUs={frame?.timestampUs ?? 0}
        onChange={setIndex}
      />

      <AngleCharts
        angles={session.angles}
        currentIndex={index}
        onSeek={setIndex}
        manualLabels={session.manualLabels}
        heuristicEvents={session.heuristicEvents}
      />
    </div>
  );
}
