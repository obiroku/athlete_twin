#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/Engine.h"
#include "Engine/TableManager.h"
#include "State/PresetData.h"
#include "State/PresetLibrary.h"
#include "State/Randomizer.h"
#include <mutex>

namespace prism
{
class PrismProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PrismProcessor();
    ~PrismProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PRISM"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "PRISM"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- message-thread API
    juce::AudioProcessorValueTreeState apvts;
    Engine engine;
    TableManager tables;
    PresetLibrary library;

    PresetData currentPreset() const;                 // snapshot of params + model
    void loadPreset (const PresetData& p, bool fade); // fade=true: 10 ms out, apply, 10 ms in
    void servicePresetLoad();                         // called by the timer (tests call it directly)
    bool presetLoadPending() const { std::lock_guard<std::mutex> g (modelLock); return pendingPreset != nullptr || waitingForTables; }
    void setExtras (const PatchExtras& x);            // routings, shapes, fx order -> audio via FIFO
    PatchExtras getExtras() const;
    void setTableSource (int osc, const TableSource& src);
    TableSource getTableSource (int osc) const;
    std::array<std::string, 4> getMacroNames() const;
    void setMacroName (int m, const std::string& n);
    juce::String getPresetName() const;
    void setPresetMeta (const std::string& name, const std::string& category);
    int addRouting (int src, int dest, float depth);  // returns slot or -1
    void removeRouting (int slot);
    void learnChord();                                // from currently held keys
    void panic();
    void collectGarbage() { tables.collectGarbage(); }

    // A/B and undo/redo (message thread)
    void captureUndoSnapshot();
    void undo();
    void redo();
    bool canUndo() const { return undoPos > 0; }
    bool canRedo() const { return undoPos + 1 < (int) undoStack.size(); }
    void toggleAB();
    int currentABSlot() const { return abSlot; }

    // MIDI learn (knob right-click): next CC moves this parameter
    void startMidiLearn (int param) { midiLearnParam.store (param); }
    std::atomic<int> midiLearnParam { -1 };

    double guiScale = 1.0;
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<int> extrasVersion { 0 };   // bumps whenever routings/shapes/order change (GUI refresh)
    // audio -> GUI scope ring (mono, written once per block)
    static constexpr int ScopeSize = 4096;
    std::array<float, ScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    juce::RangedAudioParameter* param (int idx) const { return params[(size_t) idx]; }

private:
    std::array<juce::RangedAudioParameter*, P::NumParams> params {};
    std::array<const std::atomic<float>*, P::NumParams> raw {};

    mutable std::mutex modelLock;
    PresetData model;                             // meta, extras, tables (params live in APVTS)
    std::unique_ptr<PresetData> pendingPreset;
    bool waitingForTables = false;
    juce::int64 waitStartMs = 0;

    // GUI -> audio FIFO for PatchExtras (SPSC, producers serialised by fifoLock)
    static constexpr int FifoCap = 8;
    std::array<PatchExtras, FifoCap> fifo;
    std::atomic<int> fifoW { 0 }, fifoR { 0 };
    std::mutex fifoLock;
    void pushExtras (const PatchExtras& x);

    // preset fade state machine
    enum FadeState { Idle, FadingOut, Muted, FadingIn };
    int fadeState = Idle;
    float fadeGain = 1.0f, fadeStep = 0.0f;
    std::atomic<bool> fadeRequest { false }, audioMuted { false }, presetApplied { false }, panicRequest { false };
    std::atomic<juce::int64> lastProcessMs { 0 };
    void applyPresetNow (const PresetData& p);

    // MIDI learn map: CC -> param
    std::array<std::atomic<int>, 128> ccMap;
    std::array<std::atomic<float>, 128> ccValue;
    std::array<std::atomic<bool>, 128> ccDirty;

    // undo / AB
    std::vector<juce::String> undoStack;
    int undoPos = -1;
    juce::String lastSnapshot;
    int idleTicks = 0;
    uint64_t lastStateHash = 0;
    bool lastMpe = false;
    bool snapshotDirty = false;
    int abSlot = 0;
    juce::String abStore[2];

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    double sampleRate = 48000.0;
};
} // namespace prism
