#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lens::gui::theme
{
// SPEC §12: one dark background, panels, light text, one amber accent.
inline const juce::Colour background { 0xff1c1d20 };
inline const juce::Colour panel { 0xff26282c };
inline const juce::Colour panelRaised { 0xff2f3237 };
inline const juce::Colour outline { 0xff3a3d43 };
inline const juce::Colour text { 0xffd8d8d8 };
inline const juce::Colour textDim { 0xff8d9096 };
inline const juce::Colour grid { 0xff2e3035 };
inline const juce::Colour accent { 0xfff2a33a };
inline const juce::Colour dot { 0xffbfc1c5 };

juce::Font font (float height, bool semibold = false);
} // namespace lens::gui::theme
