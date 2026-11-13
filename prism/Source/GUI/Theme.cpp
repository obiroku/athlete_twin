#include "Theme.h"
#include "BinaryData.h"

namespace prism::gui
{
int captionWidth (juce::ComboBox& box);
static juce::Typeface::Ptr interRegular()
{
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (PrismBinary::InterRegular_ttf, (size_t) PrismBinary::InterRegular_ttfSize);
    return t;
}
static juce::Typeface::Ptr interBold()
{
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor (PrismBinary::InterSemiBold_ttf, (size_t) PrismBinary::InterSemiBold_ttfSize);
    return t;
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (bold ? interBold() : interRegular()).withHeight (height));
}

PrismLookAndFeel::PrismLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::buttonOnColourId, col::accent);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bg);
    setColour (juce::ComboBox::backgroundColourId, col::panelHi);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, col::bg);
    setColour (juce::PopupMenu::headerTextColourId, col::accent);
    setColour (juce::TextEditor::backgroundColourId, col::bg);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::TextEditor::highlightColourId, col::accentDim);
    setColour (juce::CaretComponent::caretColourId, col::accent);
    setColour (juce::ListBox::backgroundColourId, col::panel);
    setColour (juce::ScrollBar::thumbColourId, col::line);
    setColour (juce::Slider::thumbColourId, col::accent);
    setColour (juce::Slider::trackColourId, col::accent);
    setColour (juce::Slider::backgroundColourId, col::line);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
    setColour (juce::TabbedComponent::backgroundColourId, col::bg);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId, col::dim);
    setColour (juce::TabbedButtonBar::frontTextColourId, col::accent);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::accent);
}

juce::Typeface::Ptr PrismLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    return f.isBold() ? interBold() : interRegular();
}

void PrismLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? col::accent : col::panelHi;
    if (over) c = c.brighter (0.08f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void PrismLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float s = juce::jmin (16.0f, r.getHeight() - 4.0f);
    auto box = juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - s / 2, s * 1.7f, s);
    g.setColour (b.getToggleState() ? col::accent : col::line);
    g.fillRoundedRectangle (box, s / 2);
    g.setColour (b.getToggleState() ? col::bg : (over ? col::text : col::dim));
    const float knobX = b.getToggleState() ? box.getRight() - s + 2 : box.getX() + 2;
    g.fillEllipse (knobX, box.getY() + 2, s - 4, s - 4);
    g.setColour (col::text);
    g.setFont (uiFont (13.0f));
    g.drawText (b.getButtonText(), r.withLeft (box.getRight() + 6), juce::Justification::centredLeft);
}

void PrismLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? col::panelHi.brighter (0.06f) : col::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto caption = box.getProperties()["caption"].toString();
    if (caption.isNotEmpty())
    {
        g.setColour (col::dim);
        g.setFont (uiFont (10.5f));
        g.drawText (caption, juce::Rectangle<int> (6, 0, captionWidth (box), h), juce::Justification::centredLeft);
    }
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (col::dim);
    g.fillPath (arrow);
}

int captionWidth (juce::ComboBox& box)
{
    const auto caption = box.getProperties()["caption"].toString();
    if (caption.isEmpty()) return 0;
    return juce::GlyphArrangement::getStringWidthInt (uiFont (10.5f), caption) + 6;
}

void PrismLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int cw = captionWidth (box);
    label.setBounds (2 + cw, 1, box.getWidth() - 22 - cw, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void PrismLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 3, (float) w, 6);
    g.setColour (col::line);
    g.fillRoundedRectangle (r, 3);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? (float) s.getPositionOfValue (0.0) : (float) x;
    g.setColour (col::accent);
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (std::min (zero, pos), r.getY(), std::max (zero, pos), r.getBottom()), 3);
}

void PrismLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds();
    const bool front = b.isFrontTab();
    g.setColour (front ? col::panel : col::bg);
    g.fillRect (r);
    if (front) { g.setColour (col::accent); g.fillRect (r.removeFromBottom (2)); }
    g.setColour (front ? col::accent : (over ? col::text : col::dim));
    g.setFont (uiFont (13.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}
} // namespace prism::gui
