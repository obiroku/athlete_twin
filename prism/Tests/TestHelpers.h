#pragma once
// Offline render helpers shared by tests, the benchmark and tools.
// Note numbers follow MIDI (60 = C3 in Logic's naming).
#include "PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

namespace prism::test
{
struct Note
{
    double start = 0.0;   // seconds
    double length = 1.0;  // seconds (note-off at start + length)
    int note = 60;
    float velocity = 0.8f;
    int channel = 1;
};

struct MidiEvent
{
    double time = 0.0;
    juce::MidiMessage msg;
};

struct HostTransport : juce::AudioPlayHead
{
    bool playing = false;
    double bpm = 120.0;
    double ppqAtZero = 0.0;
    int64_t samplePos = 0;
    double sr = 48000.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setTimeInSamples (samplePos);
        p.setPpqPosition (ppqAtZero + (double) samplePos / sr * bpm / 60.0);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

struct RenderOptions
{
    double sampleRate = 48000.0;
    std::vector<int> blocks { 512 };   // block size pattern (cycled)
    bool nonRealtime = true;
    HostTransport* transport = nullptr;
    // called before each block with the absolute sample position
    std::function<void (PrismProcessor&, int64_t)> beforeBlock;
    bool callService = true;           // run the message-thread service between blocks
};

std::unique_ptr<PrismProcessor> makeProcessor (double sr = 48000.0, int maxBlock = 512);
void setParam (PrismProcessor& p, const std::string& id, float realValue);
float getParam (PrismProcessor& p, const std::string& id);
void waitForTables (PrismProcessor& p);
void loadPresetNow (PrismProcessor& p, const PresetData& d);

// Render `seconds` of audio for the given notes / raw MIDI events
juce::AudioBuffer<float> render (PrismProcessor& p, const std::vector<Note>& notes, double seconds,
                                 const RenderOptions& opt = {}, const std::vector<MidiEvent>& extra = {});

// ---- analysis
double rms (const juce::AudioBuffer<float>& b, int ch, int start = 0, int len = -1);
double peak (const juce::AudioBuffer<float>& b, int start = 0, int len = -1);
double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b);
double maxDelta (const juce::AudioBuffer<float>& b, int ch, int start = 0, int len = -1);
// Estimated fundamental (Hz) by zero-crossing interpolation over a segment
double estimateFrequency (const float* x, int n, double sr);
// Magnitude spectrum (Blackman-Harris) in dB, n must be a power of two
std::vector<double> spectrumDb (const float* x, int n);
bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr);
} // namespace prism::test
