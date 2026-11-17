#pragma once

#include "BandPanel.h"
#include "EditorContext.h"
#include "SpectrumDisplay.h"

namespace lens::gui
{
class CoachPanel;
class PresetBrowser;

// The whole editor at its logical size (1000 × 650). The editor scales it.
class MainView final : public juce::Component
{
public:
    static constexpr int logicalWidth = 1000, logicalHeight = 650;

    explicit MainView (EditorContext&);
    ~MainView() override;

    void refresh (double nowMs); // from the editor's vblank
    void paint (juce::Graphics&) override;
    void resized() override;

    SpectrumDisplay& display() noexcept { return spectrum; }
    void setHighlights (const juce::StringArray&);
    void setStep (const juce::String& step);
    void setCoachVisible (bool);
    bool isCoachVisible() const noexcept { return coachVisible; }
    void refreshPresetLabel();

    // Hooks filled in by later phases (Coach, presets).
    std::function<void()> onPresetNameClicked, onPrevPreset, onNextPreset;
    juce::Component* coachComponent = nullptr;
    juce::Component* overlayComponent = nullptr;

private:
    void updateTopBar();
    void markHighlights();

    EditorContext& ctx;
    SpectrumDisplay spectrum;
    BandPanel bandPanel;

    // Top bar.
    juce::TextButton prevPreset { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextPreset { juce::String::fromUTF8 ("\xe2\x96\xb6") }, presetName;
    juce::Label presetNote;
    juce::ComboBox trackType;
    juce::TextButton steps[4];
    juce::ComboBox phaseMode, resolution;
    juce::Label latency, cpu;
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "Copy" }, undoBtn { "Undo" }, redoBtn { "Redo" }, coachBtn { "Coach" };

    // Output section.
    juce::Slider outGain;
    juce::Label outLabel;
    juce::ToggleButton autoGain { "Auto" }, delta { "Delta" }, bypass { "Bypass" };
    juce::Label loudness;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAtt, deltaAtt, bypassAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAtt, resAtt;

    bool coachVisible = false;
    double lastSlowRefresh = 0.0;
    juce::Rectangle<int> topBar;
};
} // namespace lens::gui
