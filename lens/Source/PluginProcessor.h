#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Engine.h"
#include "DSP/Fifo.h"
#include "Params/ParamReader.h"
#include "State/EditHistory.h"
#include "State/SessionState.h"

namespace lens
{
class LensProcessor final : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    LensProcessor();
    ~LensProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LENS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- State and presets as JSON (SPEC §10, §11). Message thread.
    juce::String getStateJson() const;
    bool setStateJson (const juce::String&);
    juce::String getPresetJson() const;
    // Applies a preset immediately (no crossfade). Bypass and the A/B
    // slot selection are left alone.
    bool setPresetJson (const juce::String&);
    // Loads a preset into the current A/B slot with a 20 ms crossfade.
    // Undoable.
    bool loadPreset (const juce::String& json);

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getApvts() const noexcept { return apvts; }

    // Non-parameter state. Copy out, edit, copy back (message thread).
    state::SessionState getSession() const;
    void setSession (const state::SessionState&);

    // ---- Band setups, undo/redo (message thread).
    juce::var getBands() const; // all 24 slots
    void applyBands (const juce::var& bands);
    // Wrap an edit: the setup before beginEdit and after endEdit becomes
    // one undo step.
    void beginEdit();
    void endEdit (const juce::String& name = {});
    void pushUndo (const juce::var& before, const juce::String& name = {});
    bool undo();
    bool redo();
    state::EditHistory& history() noexcept { return editHistory; }

    // ---- A/B (message thread).
    void copyActiveToOther();
    void copyOtherToActive();

    // ---- Learn (message thread). A second call stops early.
    void toggleLearn (int bandIndex);
    bool isLearning (int bandIndex) const;
    float learnProgress (int bandIndex) const; // 0..1
    std::function<void (int band)> onLearnFinished;

    // ---- UI state shared with the audio thread (any thread).
    std::atomic<int> soloBand { -1 }, listenBand { -1 };
    struct EarTrainerState
    {
        std::atomic<bool> on { false };
        std::atomic<float> freq { 1000.0f }, gainDb { 0.0f }, q { 1.0f };
    } earTrainer;

    dsp::Engine& getEngine() noexcept { return engine; }
    dsp::EngineStats& stats() noexcept { return engine.stats(); }
    float cpuLoad() const noexcept { return cpuLoadValue.load(); }
    bool isMono() const;
    bool hasSidechainBus() const;

    // Message-thread housekeeping (latency reports, swaps, Learn). Runs
    // on a timer; tests call it directly.
    void serviceMessageThread();

    // Reads the current parameters (any thread, allocation-free).
    void readParams (dsp::EngineParams& p) const noexcept { reader.read (p); }

private:
    void timerCallback() override { serviceMessageThread(); }
    void process (juce::AudioBuffer<float>&, bool forceBypass) noexcept;
    int activeSlot() const;
    void requestSwap (const juce::var& bands, const juce::var& globals, const juce::String& undoName = {});
    void serviceSwap();
    void serviceAB();
    void serviceLearn();

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    params::ParamReader reader;
    dsp::Engine engine;
    dsp::EngineParams blockParams {};

    // Commands from the message thread, applied at block start.
    enum class Command : int
    {
        BeginSwap,
        EndSwap
    };
    dsp::SpscQueue<Command> commands { 64 };

    // Guards `session` between the message thread and whichever thread
    // the host uses for get/setStateInformation. Never taken on the
    // audio thread.
    juce::CriticalSection sessionLock;
    state::SessionState session;

    state::EditHistory editHistory;
    juce::var editBefore;
    int editDepth = 0;

    struct PendingSwap
    {
        bool active = false;
        bool sent = false;
        juce::var bands, globals;
        double startMs = 0.0;
        bool recordUndo = false;
        juce::var undoBefore;
        juce::String undoName;
    } pendingSwap;
    int lastSlot = 0;

    std::array<bool, dsp::kNumBands> learnPending {};
    std::array<juce::var, dsp::kNumBands> learnBefore;

    std::atomic<double> lastProcessMs { 0.0 };
    std::atomic<float> cpuLoadValue { 0.0f };
    std::atomic<bool> preparedFlag { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LensProcessor)
};
} // namespace lens
