#pragma once
#include "PluginProcessor.h"

namespace prism
{
class PrismEditor : public juce::AudioProcessorEditor
{
public:
    explicit PrismEditor (PrismProcessor& p) : AudioProcessorEditor (p), generic (p)
    {
        addAndMakeVisible (generic);
        setSize (1000, 700);
    }
    void resized() override { generic.setBounds (getLocalBounds()); }
private:
    juce::GenericAudioProcessorEditor generic;
};
} // namespace prism
