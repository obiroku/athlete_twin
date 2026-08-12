import { useState } from "react";
import { AthleteWorkspace } from "./components/AthleteWorkspace";
import { ComparisonView } from "./components/ComparisonView";
import { VideoLoader } from "./components/VideoLoader";
import type { AthleteSession } from "./lib/types";

type Slot = "a" | "b";

function App() {
  const [sessions, setSessions] = useState<Record<Slot, AthleteSession | null>>({
    a: null,
    b: null,
  });
  const [tab, setTab] = useState<"analyze" | "compare">("analyze");
  const [activeSlot, setActiveSlot] = useState<Slot>("a");

  const setSession = (slot: Slot, session: AthleteSession) =>
    setSessions((prev) => ({ ...prev, [slot]: session }));

  return (
    <div style={{ maxWidth: 900, margin: "0 auto", padding: 16 }}>
      <header style={{ marginBottom: 16 }}>
        <h1 style={{ fontSize: 18, margin: 0 }}>Athlete Twin — Milestone 0 diagnostic rig</h1>
        <p style={{ opacity: 0.7, margin: "4px 0" }}>
          Disposable research code. Question: does side-on phone video contain stable,
          athlete-specific movement signatures? All processing is on-device — video never leaves
          this browser.
        </p>
      </header>

      <nav style={{ display: "flex", gap: 8, marginBottom: 16 }}>
        <button onClick={() => setTab("analyze")} disabled={tab === "analyze"}>
          analyze
        </button>
        <button onClick={() => setTab("compare")} disabled={tab === "compare"}>
          compare
        </button>
      </nav>

      {tab === "analyze" && (
        <div style={{ display: "flex", flexDirection: "column", gap: 24 }}>
          <div style={{ display: "flex", gap: 8 }}>
            {(["a", "b"] as Slot[]).map((slot) => (
              <button
                key={slot}
                onClick={() => setActiveSlot(slot)}
                disabled={activeSlot === slot}
              >
                athlete {slot.toUpperCase()}
                {sessions[slot] ? ` — ${sessions[slot]!.label}` : ""}
              </button>
            ))}
          </div>

          {!sessions[activeSlot] && (
            <VideoLoader
              label={`athlete ${activeSlot.toUpperCase()}`}
              onLoaded={(session) => setSession(activeSlot, session)}
            />
          )}

          {sessions[activeSlot] && (
            <AthleteWorkspace
              session={sessions[activeSlot]!}
              onChange={(session) => setSession(activeSlot, session)}
            />
          )}
        </div>
      )}

      {tab === "compare" && (
        <div>
          {sessions.a && sessions.b ? (
            <ComparisonView sessionA={sessions.a} sessionB={sessions.b} />
          ) : (
            <p>Load both athlete A and athlete B in the "analyze" tab first.</p>
          )}
        </div>
      )}
    </div>
  );
}

export default App;
