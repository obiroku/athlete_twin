#pragma once
#include "PluginProcessor.h"
#include "GUI/TopBar.h"
#include "GUI/WavetablePage.h"

namespace prism
{
class PrismEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer
{
public:
    explicit PrismEditor (PrismProcessor& p);
    ~PrismEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setScale (double s);
    void showTab (int i) { tabs.setCurrentTabIndex (i); }
    void tickForTest() { onVBlank (lastFrame + 1.0); }

    static constexpr int BaseW = 1000, BaseH = 700;

private:
    PrismProcessor& proc;
    gui::PrismLookAndFeel lnf;
    juce::Component content;              // laid out at 1000 x 700, scaled with a transform
    gui::PresetBrowser browser;
    gui::TopBar topBar;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<gui::Page*> pages;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    juce::VBlankAttachment vblank;
    double lastFrame = 0.0;
    std::vector<gui::LiveComponent*> live;
    int liveTab = -1;
    void onVBlank (double timestamp);
    void mouseDown (const juce::MouseEvent&) override;
};
} // namespace prism
