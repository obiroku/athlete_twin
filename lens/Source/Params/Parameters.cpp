#include "Parameters.h"

#include <cmath>

namespace lens::params
{
namespace
{
using Float = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Bool = juce::AudioParameterBool;
using FloatAttr = juce::AudioParameterFloatAttributes;

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

juce::NormalisableRange<float> linRange (float lo, float hi)
{
    return { lo, hi };
}

juce::ParameterID pid (const juce::String& s)
{
    return { s, versionHint };
}

std::unique_ptr<juce::AudioProcessorParameterGroup> makeBandGroup (int b)
{
    const auto num = juce::String (b).paddedLeft ('0', 2);
    const auto prefix = "B" + num + " ";
    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + num, "Band " + num, " | ");

    auto idOf = [b] (const char* suffix) { return pid (bandParamId (b, suffix)); };
    auto addFloat = [&] (const char* suffix, const char* name, juce::NormalisableRange<float> range, float def, const char* unit)
    {
        group->addChild (std::make_unique<Float> (idOf (suffix), prefix + name, std::move (range), def, FloatAttr().withLabel (unit)));
    };
    auto addChoice = [&] (const char* suffix, const char* name, const juce::StringArray& choices, int def)
    {
        group->addChild (std::make_unique<Choice> (idOf (suffix), prefix + name, choices, def));
    };
    auto addBool = [&] (const char* suffix, const char* name, bool def)
    {
        group->addChild (std::make_unique<Bool> (idOf (suffix), prefix + name, def));
    };

    addBool (band::on, "On", false);
    addChoice (band::shape, "Shape", { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt", "All Pass" }, 0);
    addFloat (band::freq, "Freq", logRange (10.0f, 30000.0f), defaultBandFrequency (b), "Hz");
    addFloat (band::gain, "Gain", linRange (-30.0f, 30.0f), 0.0f, "dB");
    addFloat (band::q, "Q", logRange (0.025f, 40.0f), 1.0f, "");
    addChoice (band::slope, "Slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" }, 1);
    addChoice (band::stereo, "Stereo", { "Stereo", "Mid", "Side", "Left", "Right" }, 0);

    addBool (band::dynOn, "Dyn On", false);
    addChoice (band::dynMode, "Dyn Mode", { "Above", "Below" }, 0);
    addFloat (band::dynRange, "Dyn Range", linRange (-30.0f, 30.0f), -6.0f, "dB");
    addFloat (band::dynThresh, "Dyn Threshold", linRange (-60.0f, 0.0f), -20.0f, "dB");
    {
        auto ratio = linRange (1.5f, 10.0f);
        ratio.setSkewForCentre (3.0f);
        addFloat (band::dynRatio, "Dyn Ratio", ratio, 3.0f, "");
    }
    addFloat (band::dynAttack, "Dyn Attack", logRange (0.1f, 500.0f), 10.0f, "ms");
    addFloat (band::dynRelease, "Dyn Release", logRange (10.0f, 5000.0f), 120.0f, "ms");

    addChoice (band::detSource, "Det Source", { "Internal", "External" }, 0);
    addBool (band::detCustom, "Det Custom", false);
    addFloat (band::detHp, "Det HP", logRange (10.0f, 30000.0f), 20.0f, "Hz");
    addFloat (band::detLp, "Det LP", logRange (10.0f, 30000.0f), 20000.0f, "Hz");

    addBool (band::specOn, "Spectral On", false);
    addFloat (band::specThresh, "Spectral Threshold", linRange (0.0f, 24.0f), 6.0f, "dB");
    addFloat (band::specAmount, "Spectral Amount", linRange (0.0f, 24.0f), 12.0f, "dB");

    return group;
}

std::unique_ptr<juce::AudioProcessorParameterGroup> makeGlobalGroup()
{
    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");

    group->addChild (std::make_unique<Choice> (pid (id::phaseMode), "Phase Mode", juce::StringArray { "Zero Latency", "Linear Phase" }, 0));
    group->addChild (std::make_unique<Choice> (pid (id::lpResolution), "LP Resolution", juce::StringArray { "Low", "Medium", "High" }, 1));
    group->addChild (std::make_unique<Choice> (pid (id::character), "Character", juce::StringArray { "Clean", "Warm" }, 0));
    group->addChild (std::make_unique<Float> (pid (id::outputGain), "Output Gain", linRange (-30.0f, 30.0f), 0.0f, FloatAttr().withLabel ("dB")));
    group->addChild (std::make_unique<Bool> (pid (id::autoGain), "Auto Gain", true));
    group->addChild (std::make_unique<Bool> (pid (id::delta), "Delta", false));
    group->addChild (std::make_unique<Bool> (pid (id::bypass), "Bypass", false));
    group->addChild (std::make_unique<Choice> (pid (id::abSlot), "A/B Slot", juce::StringArray { "A", "B" }, 0));
    group->addChild (std::make_unique<Float> (pid (id::lookahead), "Lookahead", linRange (0.0f, 10.0f), 0.0f, FloatAttr().withLabel ("ms")));

    return group;
}
} // namespace

const juce::StringArray& bandSuffixes()
{
    static const juce::StringArray s {
        band::on, band::shape, band::freq, band::gain, band::q, band::slope, band::stereo,
        band::dynOn, band::dynMode, band::dynRange, band::dynThresh, band::dynRatio, band::dynAttack, band::dynRelease,
        band::detSource, band::detCustom, band::detHp, band::detLp,
        band::specOn, band::specThresh, band::specAmount
    };
    return s;
}

const juce::StringArray& globalIds()
{
    static const juce::StringArray s {
        id::phaseMode, id::lpResolution, id::character, id::outputGain, id::autoGain,
        id::delta, id::bypass, id::abSlot, id::lookahead
    };
    return s;
}

juce::String bandParamId (int b, juce::StringRef suffix)
{
    jassert (b >= 1 && b <= numBands);
    return "b" + juce::String (b).paddedLeft ('0', 2) + "_" + suffix;
}

float defaultBandFrequency (int b)
{
    return juce::jmin (20000.0f, 50.0f * std::pow (2.0f, (float) (b - 1) / 3.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeGlobalGroup());
    for (int b = 1; b <= numBands; ++b)
        layout.add (makeBandGroup (b));
    return layout;
}

juce::String choiceToken (const juce::AudioParameterChoice& p, int index)
{
    const auto name = p.choices[index];
    const auto& pidStr = p.paramID;
    if (pidStr.endsWith ("_shape"))
        return name.removeCharacters (" ");
    if (pidStr.endsWith ("_slope"))
        return name.upToFirstOccurrenceOf (" dB", false, false);
    return name;
}

int choiceIndexForToken (const juce::AudioParameterChoice& p, const juce::String& token)
{
    for (int i = 0; i < p.choices.size(); ++i)
        if (choiceToken (p, i) == token)
            return i;
    return -1;
}
} // namespace lens::params
