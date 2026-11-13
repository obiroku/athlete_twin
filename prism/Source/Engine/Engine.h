#pragma once
// PRISM synth engine: global control pass, voice manager, FX rack and master.
// Headless (no GUI) and deterministic: everything runs on a sample counter.
#include "Voice.h"
#include "Arp.h"
#include "../DSP/FxRack.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>

namespace prism
{
class Engine
{
public:
    static constexpr int MaxVoices = 32;
    static constexpr int TailVoices = 4;
    static constexpr int PoolSize = MaxVoices + TailVoices;

    struct Transport { bool playing = false, ppqValid = false; double ppq = 0.0, bpm = 120.0; };

    Engine();
    void prepare (double sampleRate, int maxBlock);
    void setParamSource (const std::atomic<float>* const* raw) noexcept { rawParams = raw; }
    void setExtras (const PatchExtras& x) noexcept;
    const PatchExtras& getExtras() const noexcept { return extras; }
    void setTable (int osc, const dsp::Wavetable* t) noexcept { tables[osc] = t; }

    void process (float* L, float* R, int n, const juce::MidiBuffer& midi, const Transport& tr) noexcept;

    void resetSeeds() noexcept;        // seeds + free LFO phases + arp (prepare & transport start)
    void resetTails() noexcept;        // FX tails + limiter
    void panic() noexcept;             // kill all voices immediately
    void releaseAll() noexcept;

    // ---- telemetry (audio thread writes once per block, GUI reads)
    std::atomic<int> voiceCount { 0 };
    std::atomic<float> playhead[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    std::atomic<float> liveNorm[P::NumParams];
    std::atomic<bool> liveValid[P::NumParams];
    std::atomic<float> liveSrc[NumSrc];
    std::atomic<uint64_t> heldMask[2] { { 0 }, { 0 } };  // keys currently down (for chord learn)
    std::atomic<double> tailSecs { 0.0 };
    std::atomic<float> preLimiterPeak { 0.0f };
    std::atomic<float> modWheelValue { 0.0f };

    int latencySamples() const noexcept { return dsp::Master::LimiterLookahead; }
    bool tablesReady() const noexcept { return true; }

private:
    double sr = 48000.0;
    int maxBlock = 512;
    const std::atomic<float>* const* rawParams = nullptr;
    std::array<float, P::NumParams> fallbackRaw {};
    const dsp::Wavetable* tables[2] { nullptr, nullptr };
    PatchExtras extras;
    RoutingTable routes;
    dsp::RemapCurve remap[2];

    // ---- parameters / smoothing
    std::array<float, P::NumParams> lastRaw {}, target {}, cur {}, delta {};
    std::array<int, P::NumParams> stepsLeft {};
    int smoothTicks = 30;
    bool firstTick = true;
    std::vector<float> rows;              // (maxTicks + 2) x NumParams
    std::array<float, P::NumParams> prevRow {}, lastRow {};
    int64_t absSample = 0;

    // ---- global sources (per sample, for the current chunk)
    std::vector<float> gsrc[NumSrc];
    std::vector<float> baseFast[NumFast];
    float midiTarget[NumSrc] {}, midiSm[NumSrc] {};
    float midiSmCoef = 0.99f;
    dsp::Lfo gLfo[4];
    bool gLfoFree[4] {};

    // ---- voices
    std::vector<Voice> voices;
    uint64_t orderCounter = 0;
    dsp::Rng noteRng { 0x9E57 };
    float alternateState = 1.0f;
    float lastNotePitch = -1.0f;
    struct Key { int note; float vel; uint8_t ch; };
    std::array<Key, 128> monoStack {};
    int monoCount = 0;
    int monoVoice = -1;
    bool sustain = false;
    std::array<bool, 128 * 16> sustained {};
    std::array<bool, 128> keyDown {};

    // ---- arp / chord
    Arp arp;
    ChordMode chord;
    ChunkEvents rawEv, chordEv, arpEv;
    bool wasPlaying = false;

    // ---- FX / master
    dsp::FxRack rack;
    dsp::Master master;
    std::array<int, dsp::FxRack::NumFx> fxEnable {};
    std::array<float, P::NumParams> gNorm {}, gReal {}, gLastNorm {};
    std::vector<float> mixL, mixR;
    float newestSnap[NumSrc] {};

    RenderContext ctx;
    struct BlockEvents { std::array<Event, 4096> ev {}; int count = 0; } blockEv;

    void processChunk (float* L, float* R, int n, const Transport& tr, double ppqAtStart) noexcept;
    void computeTick (float* row) noexcept;
    const float* rowAt (int pos, int tickPhase) const noexcept { return rows.data() + (size_t) ((pos + tickPhase) / kTick + 1) * P::NumParams; }
    float realOf (const float* row, int p) const noexcept { return paramDefs()[(size_t) p].fromNorm (row[p]); }

    void handleNoteOn (int pos, int note, float vel, int ch) noexcept;
    void handleNoteOff (int pos, int note, float relVel, int ch) noexcept;
    int allocVoice() noexcept;
    int activeCount() const noexcept;
    void startVoice (int vi, int pos, int note, float vel, int ch, bool glide, float glideFrom) noexcept;
    int monoSelect() const noexcept;
    void renderVoices (int a, int b) noexcept;
    void updateTelemetry() noexcept;
};
} // namespace prism
