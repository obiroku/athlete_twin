#include "Explain.h"

#include "Util/Format.h"

namespace lens::coach
{
namespace
{
// Replaces "{a|b|c}" choices: picks option `index`.
juce::String choose (const juce::String& text, const juce::String& options, int index)
{
    const auto parts = juce::StringArray::fromTokens (options.substring (1, options.length() - 1), "|", "");
    return text.replace (options, parts[juce::jlimit (0, parts.size() - 1, index)]);
}

juce::String chooseFirst (juce::String text, int index)
{
    // The first {..|..} group in the text.
    const int start = text.indexOf ("{");
    while (start >= 0)
    {
        const int end = text.indexOf (start, "}");
        const auto group = text.substring (start, end + 1);
        if (group.contains ("|"))
            return choose (text, group, index);
        break;
    }
    return text;
}

juce::String chooseGroup (juce::String text, const juce::String& firstOption, int index)
{
    const int start = text.indexOf ("{" + firstOption + "|");
    if (start < 0)
        return text;
    const int end = text.indexOf (start, "}");
    return choose (text, text.substring (start, end + 1), index);
}
} // namespace

juce::String widthPhrase (double q, const Content& c)
{
    for (const auto& w : c.width)
        if (q >= w.min && q < w.max)
            return w.text;
    return c.width.empty() ? juce::String() : c.width.back().text;
}

juce::String explain (const dsp::BandParams& b, const juce::String& trackType, float activity, int numChannels, const Content& c)
{
    using dsp::Shape;
    const auto freq = fmt::freq (b.freq);
    const auto zone = c.zoneAt (trackType, b.freq);
    juce::String s;

    if (b.isDynamic())
    {
        const bool above = b.dynMode == dsp::DynMode::Above;
        const bool cut = b.dynRange <= 0.0f;
        const juce::String key = above ? (cut ? "dyn_above_cut" : "dyn_above_boost") : (cut ? "dyn_below_cut" : "dyn_below_boost");
        s = c.template_ (key);

        juce::String detector;
        if (b.detExternal)
            detector = c.template_ ("detector_external");
        else if (b.detCustom)
            detector = c.template_ ("detector_custom").replace ("{hp}", fmt::freq (b.detHp)).replace ("{lp}", fmt::freq (b.detLp));
        else
            detector = c.template_ ("detector_internal");

        if (b.shape == Shape::LowShelf || b.shape == Shape::HighShelf)
        {
            const auto from = c.template_ ("shelf_dynamic_replace");
            const auto to = c.template_ (b.shape == Shape::LowShelf ? "shelf_dynamic_low" : "shelf_dynamic_high");
            s = s.replace (from, to);
        }

        const int act = (int) std::lround (activity);
        s = s.replace ("{range}", fmt::gain (b.dynRange))
                .replace ("{freq}", freq)
                .replace ("{zone}", zone)
                .replace ("{detector}", detector)
                .replace ("{thresh}", fmt::number (b.dynThresh, 1))
                .replace ("{activity}", juce::String (act));
        if (act <= 0)
            s << c.template_ ("activity_none");
        else if (act >= 90)
            s << c.template_ ("activity_always");
    }
    else
    {
        switch (b.shape)
        {
            case Shape::Bell:
            {
                const double g = std::round (b.gain * 10.0) / 10.0;
                s = c.template_ (g > 0.0 ? "bell_boost" : (g < 0.0 ? "bell_cut" : "bell_zero"));
                s = s.replace ("{gain}", fmt::gain (b.gain)).replace ("{freq}", freq).replace ("{zone}", zone).replace ("{width}", widthPhrase (b.q, c));
                break;
            }
            case Shape::LowShelf:
            case Shape::HighShelf:
                s = c.template_ (b.shape == Shape::LowShelf ? "lowshelf" : "highshelf");
                s = chooseGroup (s, "Raises", b.gain >= 0.0f ? 0 : 1).replace ("{freq}", freq).replace ("{gain}", fmt::gain (b.gain));
                break;
            case Shape::LowCut:
            case Shape::HighCut:
                s = c.template_ (b.shape == Shape::LowCut ? "lowcut" : "highcut");
                s = s.replace ("{freq}", freq)
                        .replace ("{slope}", b.slope == dsp::kSlopeBrickwall ? juce::String ("TODO: brickwall") : juce::String (dsp::slopeDbPerOct (b.slope)));
                break;
            case Shape::Notch: s = c.template_ ("notch").replace ("{freq}", freq); break;
            case Shape::BandPass: s = c.template_ ("bandpass").replace ("{freq}", freq); break;
            case Shape::AllPass: s = c.template_ ("allpass").replace ("{freq}", freq); break;
            case Shape::Tilt:
            {
                const int bright = b.gain >= 0.0f ? 0 : 1;
                s = c.template_ ("tilt");
                s = chooseGroup (s, "brighter", bright);
                s = chooseGroup (s, "up", bright);
                s = chooseGroup (s, "down", bright);
                s = s.replace ("{freq}", freq).replace ("{half}", fmt::plain (b.gain / 2.0));
                break;
            }
        }
    }

    if (b.stereo != dsp::StereoMode::Stereo && numChannels > 1)
        s << chooseFirst (c.template_ ("stereo_suffix"), (int) b.stereo - 1);
    return s;
}
} // namespace lens::coach
