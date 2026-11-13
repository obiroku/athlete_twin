#pragma once
// Arpeggiator and chord mode (SPEC §7). Both transform the event stream of a
// chunk before it reaches the voice manager. No allocation; fixed arrays.
#include "Events.h"
#include "ModTypes.h"
#include "../DSP/Random.h"

namespace prism
{
using ChunkEvents = EventList<2048>;

struct ArpSettings
{
    bool on = false;
    int mode = 0, octaves = 1, div = 21, velMode = 0;
    float gate = 0.8f, swing = 0.0f, fixedVel = 0.8f;
    bool latch = false;
};

class Arp
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
    void reset() noexcept;
    void restart() noexcept;   // transport start: step counter + seed
    // in: events of the chunk (notes are consumed when the arp is on). out: generated events.
    void process (const ChunkEvents& in, ChunkEvents& out, int n, const ArpSettings& s, const PatchExtras& x,
                  bool playing, bool ppqValid, double ppqAtStart, double bpm) noexcept;
    bool hasNotes() const noexcept { return numHeld > 0; }

private:
    double sr = 48000.0;
    struct Held { int note; float vel; uint8_t ch; };
    Held held[64] {};
    int numHeld = 0;
    int physicalDown = 0;
    bool wasOn = false;
    int64_t step = 0;
    double freeBeat = 0.0;     // internal clock when the host is stopped
    bool freeRunning = false;
    bool upDownRising = true;
    dsp::Rng rng { 0xA4B };
    struct Pending { double beatOff; int note; uint8_t ch; bool active; };
    Pending offs[128] {};
    int64_t lastStepEmitted = -1;

    void addHeld (int note, float vel, uint8_t ch) noexcept;
    void removeHeld (int note) noexcept;
    void killAll (ChunkEvents& out, int pos) noexcept;
};

class ChordMode
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
    void reset() noexcept { numPending = 0; flip = false; }
    void process (const ChunkEvents& in, ChunkEvents& out, int n, bool on, float strumMs, int dir,
                  const ChordShapeData& shape) noexcept;
private:
    double sr = 48000.0;
    struct Pending { int64_t delay; Event e; };
    Pending pending[256] {};
    int numPending = 0;
    bool flip = false;
};
} // namespace prism
