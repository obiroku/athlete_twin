#include "LookAndFeel.h"

#include <BinaryData.h>

namespace lens::gui
{
juce::Font theme::font (float height, bool semibold)
{
    auto tf = semibold ? LensLookAndFeel::semiboldTypeface() : LensLookAndFeel::regularTypeface();
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

juce::Typeface::Ptr LensLookAndFeel::regularTypeface()
{
    static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    return tf;
}

juce::Typeface::Ptr LensLookAndFeel::semiboldTypeface()
{
    static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    return tf;
}

LensLookAndFeel::LensLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::Label::backgroundWhenEditingColourId, background);
    setColour (juce::Label::outlineWhenEditingColourId, accent);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::ToggleButton::textColourId, text);
}

juce::Typeface::Ptr LensLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName().isEmpty())
        return f.isBold() ? semiboldTypeface() : regularTypeface();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void LensLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float thick = std::max (2.0f, r * 0.16f);
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, start, end, true);
    g.setColour (theme::outline);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar ranges fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    if (s.isEnabled())
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (theme::accent);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (s.isEnabled() ? theme::text : theme::textDim);
    const auto tip = c.getPointOnCircumference (r - thick * 2.4f, angle);
    g.drawLine ({ c.getPointOnCircumference (r * 0.15f, angle), tip }, 2.0f);
}

void LensLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? theme::accent : bg;
    if (! b.isEnabled())
        c = c.withAlpha (0.4f);
    else if (down)
        c = c.brighter (0.15f);
    else if (highlighted)
        c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    if (b.getProperties().getWithDefault ("highlight", false))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.5f);
    }
}

void LensLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto c = b.getToggleState() ? theme::background : theme::text;
    g.setColour (b.isEnabled() ? c : c.withAlpha (0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LensLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    // Toggles look like pill buttons that light up when on.
    drawButtonBackground (g, b, theme::panelRaised, highlighted, down);
    g.setFont (theme::font (12.0f, true));
    auto c = b.getToggleState() ? theme::background : theme::text;
    g.setColour (b.isEnabled() ? c : c.withAlpha (0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LensLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r, 4.0f);
    if (box.getProperties().getWithDefault ("highlight", false))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.5f);
    }
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (theme::textDim.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}

juce::Font LensLookAndFeel::getComboBoxFont (juce::ComboBox&) { return theme::font (12.5f); }
juce::Font LensLookAndFeel::getPopupMenuFont() { return theme::font (13.5f); }
juce::Font LensLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return theme::font (std::min (13.0f, h * 0.55f), true); }
juce::Font LensLookAndFeel::getLabelFont (juce::Label& l) { return theme::font (l.getFont().getHeight()); }

void LensLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 20, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void LensLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 5.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 5.0f, 1.0f);
    juce::AttributedString s;
    s.append (text, theme::font (13.0f), theme::text);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (s, (float) w - 16.0f);
    tl.draw (g, { 8.0f, 6.0f, (float) w - 16.0f, (float) h - 12.0f });
}

juce::Rectangle<int> LensLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s;
    s.append (text, theme::font (13.0f), theme::text);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (s, 260.0f);
    const int w = (int) std::ceil (std::min (260.0f, tl.getWidth())) + 16;
    const int h = (int) std::ceil (tl.getHeight()) + 12;
    return juce::Rectangle<int> (pos.x + 12, pos.y + 16, w, h).constrainedWithin (parent);
}

juce::Slider::SliderLayout LensLookAndFeel::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    auto r = s.getLocalBounds();
    if (s.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        l.textBoxBounds = r.removeFromBottom (16);
        l.sliderBounds = r;
        return l;
    }
    return LookAndFeel_V4::getSliderLayout (s);
}

juce::Label* LensLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (theme::font (11.5f));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}
} // namespace lens::gui
