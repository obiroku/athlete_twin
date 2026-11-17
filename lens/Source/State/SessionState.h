#pragma once

#include <juce_core/juce_core.h>

namespace lens::state
{
// Everything saved in the plugin state that is not a host parameter
// (SPEC §10 "State", §11). Message-thread data: the audio thread never
// reads it.
struct SessionState
{
    // Preset metadata (§11).
    juce::String presetName { "Init" };
    juce::String category { "Init" };
    juce::String note { "A clean start." };
    juce::String description;

    juce::String trackType { "Bus" };
    juce::String workflowStep { "CLEAN" };

    // Analyzer settings (§6).
    juce::String analyzerSmoothing { "1/6" };
    juce::String analyzerSpeed { "medium" };
    double analyzerTilt = 4.5;
    bool showPre = true, showPost = true, showSidechain = true;

    double guiScale = 1.0;

    // Band setups of both A/B slots, as preset "bands" arrays. The active
    // slot is refreshed from the live parameters whenever state is saved.
    juce::var abBands[2];

    static const juce::StringArray& trackTypes();
    static const juce::StringArray& workflowSteps();
    static const juce::StringArray& categories();
    static const juce::StringArray& smoothings();
    static const juce::StringArray& speeds();

    // Preset metadata only.
    void writePresetMeta (juce::DynamicObject&) const;
    void readPresetMeta (const juce::var& root);

    // State-only extras (analyzer, GUI scale, workflow step).
    void writeStateExtras (juce::DynamicObject&) const;
    void readStateExtras (const juce::var& root);
};
} // namespace lens::state
