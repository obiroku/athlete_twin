#pragma once

#include "EditorContext.h"
#include "Theme.h"

namespace lens::gui
{
// Bottom panel: the selected band, and its dynamics when the band is
// dynamic (SPEC §12 Layout).
class BandPanel final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit BandPanel (EditorContext&);
    ~BandPanel() override;

    void refresh(); // vblank: activity, Learn progress, enablement
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateHighlights();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { bind(); }
    void bind();
    void layoutKnob (juce::Slider&, juce::Label&, juce::Rectangle<int>);

    EditorContext& ctx;
    int band = -1;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Label title, hint;
    juce::ToggleButton onBtn { "On" }, dynBtn { "Dynamic" }, soloBtn { "Solo" };
    juce::ComboBox shapeBox, slopeBox, stereoBox;
    juce::Slider freq, gain, q;
    juce::Label freqL, gainL, qL;

    // Dynamics.
    juce::ComboBox modeBox, sourceBox;
    juce::Slider range, thresh, ratio, attack, release, hp, lp;
    juce::Label rangeL, threshL, ratioL, attackL, releaseL, hpL, lpL, activity, dynTitle;
    juce::TextButton learnBtn { "Learn" };
    juce::ToggleButton customBtn { "Custom" }, listenBtn { "Listen" };

    std::vector<std::unique_ptr<SA>> sliderAtt;
    std::vector<std::unique_ptr<CA>> comboAtt;
    std::vector<std::unique_ptr<BA>> buttonAtt;

    juce::Rectangle<int> bandArea, dynArea;
    bool dynamicShown = false;
};
} // namespace lens::gui
