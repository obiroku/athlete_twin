#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "State/SessionState.h"

namespace lens
{
class LensProcessor final : public juce::AudioProcessor
{
public:
    LensProcessor();
    ~LensProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LENS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // State and presets as JSON (SPEC §10, §11). Message thread.
    juce::String getStateJson() const;
    bool setStateJson (const juce::String&);
    juce::String getPresetJson() const;
    // Loads a preset into the current A/B slot. Bypass and the A/B slot
    // selection are left alone.
    bool setPresetJson (const juce::String&);

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getApvts() const noexcept { return apvts; }

    // Non-parameter state. Copy out, edit, copy back (message thread).
    state::SessionState getSession() const;
    void setSession (const state::SessionState&);

private:
    int activeSlot() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    // Guards `session` between the message thread and whichever thread
    // the host uses for get/setStateInformation. Never taken on the
    // audio thread.
    juce::CriticalSection sessionLock;
    state::SessionState session;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LensProcessor)
};
} // namespace lens
