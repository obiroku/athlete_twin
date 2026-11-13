#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

namespace prism::gui
{
namespace col
{
    const juce::Colour bg { 0xff1c1d20 };
    const juce::Colour panel { 0xff26282c };
    const juce::Colour panelHi { 0xff2e3035 };
    const juce::Colour text { 0xffd8d8d8 };
    const juce::Colour dim { 0xff8a8c91 };
    const juce::Colour line { 0xff3a3d42 };
    const juce::Colour accent { 0xfff2a33a };
    const juce::Colour accentDim { 0x66f2a33a };
}

juce::Font uiFont (float height, bool bold = false);

class PrismLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PrismLookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (13.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return uiFont (13.0f); }
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 110; }
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
};
} // namespace prism::gui
