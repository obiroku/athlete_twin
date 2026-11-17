#pragma once

#include "EditorContext.h"
#include "LookAndFeel.h"
#include "MainView.h"

namespace lens::gui
{
class CoachPanel;
class PresetBrowser;

class LensEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LensEditor (LensProcessor&);
    ~LensEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    MainView& view() noexcept { return *main; }
    EditorContext& context() noexcept { return ctx; }

private:
    void onVBlank();

    LensProcessor& proc;
    LensLookAndFeel lnf;
    AnalyzerThread analyzerThread;
    EditorContext ctx;
    std::unique_ptr<MainView> main;
    std::unique_ptr<juce::Component> coach, browser;
    juce::TooltipWindow tooltips { this, 700 };
    juce::VBlankAttachment vblank;
    double lastFrameMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LensEditor)
};
} // namespace lens::gui
