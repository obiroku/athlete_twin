#pragma once

#include "CoachContent.h"
#include "DSP/Types.h"

namespace lens::coach
{
// The Explain line: one plain sentence about a band, built only from the
// LESSONS.md templates (§1).
juce::String explain (const dsp::BandParams&, const juce::String& trackType, float activityPercent, int numChannels = 2,
                      const Content& = Content::get());

juce::String widthPhrase (double q, const Content& = Content::get());
} // namespace lens::coach
