#include "SessionState.h"

namespace lens::state
{
namespace
{
// Reads a string property, accepting it only if it is one of `allowed`
// (or anything, when `allowed` is null).
void readToken (const juce::var& obj, const char* key, juce::String& dest, const juce::StringArray* allowed = nullptr)
{
    const auto& v = obj[key];
    if (! v.isString())
        return;
    const auto s = v.toString();
    if (allowed == nullptr || allowed->contains (s))
        dest = s;
}

void readBool (const juce::var& obj, const char* key, bool& dest)
{
    if (obj[key].isBool())
        dest = (bool) obj[key];
}
} // namespace

const juce::StringArray& SessionState::trackTypes()
{
    static const juce::StringArray s { "Bass", "Kick & Drums", "Vocal", "Choir & Vocal Bus", "Keys",
                                       "Guitar", "Synth & Pad", "Bus", "Master" };
    return s;
}

const juce::StringArray& SessionState::workflowSteps()
{
    static const juce::StringArray s { "CLEAN", "CONTROL", "SHAPE", "CHECK" };
    return s;
}

const juce::StringArray& SessionState::categories()
{
    static const juce::StringArray s { "Init", "Bass", "Drums", "Vocal", "Keys", "Guitar", "Synth", "Bus", "Master" };
    return s;
}

const juce::StringArray& SessionState::smoothings()
{
    static const juce::StringArray s { "raw", "1/12", "1/6", "1/3" };
    return s;
}

const juce::StringArray& SessionState::speeds()
{
    static const juce::StringArray s { "slow", "medium", "fast", "very fast" };
    return s;
}

void SessionState::writePresetMeta (juce::DynamicObject& o) const
{
    o.setProperty ("name", presetName);
    o.setProperty ("category", category);
    o.setProperty ("track_type", trackType);
    o.setProperty ("note", note);
    o.setProperty ("description", description);
}

void SessionState::readPresetMeta (const juce::var& root)
{
    readToken (root, "name", presetName);
    readToken (root, "category", category, &categories());
    readToken (root, "track_type", trackType, &trackTypes());
    readToken (root, "note", note);
    readToken (root, "description", description);
}

void SessionState::writeStateExtras (juce::DynamicObject& o) const
{
    o.setProperty ("workflow_step", workflowStep);

    auto* an = new juce::DynamicObject();
    an->setProperty ("smoothing", analyzerSmoothing);
    an->setProperty ("speed", analyzerSpeed);
    an->setProperty ("tilt", analyzerTilt);
    an->setProperty ("pre", showPre);
    an->setProperty ("post", showPost);
    an->setProperty ("sidechain", showSidechain);
    o.setProperty ("analyzer", juce::var (an));

    o.setProperty ("gui_scale", guiScale);
}

void SessionState::readStateExtras (const juce::var& root)
{
    readToken (root, "workflow_step", workflowStep, &workflowSteps());

    const auto& an = root["analyzer"];
    if (an.isObject())
    {
        readToken (an, "smoothing", analyzerSmoothing, &smoothings());
        readToken (an, "speed", analyzerSpeed, &speeds());
        if (an["tilt"].isDouble() || an["tilt"].isInt())
        {
            const double t = an["tilt"];
            if (juce::exactlyEqual (t, 0.0) || juce::exactlyEqual (t, 3.0) || juce::exactlyEqual (t, 4.5))
                analyzerTilt = t;
        }
        readBool (an, "pre", showPre);
        readBool (an, "post", showPost);
        readBool (an, "sidechain", showSidechain);
    }

    const auto& gs = root["gui_scale"];
    if (gs.isDouble() || gs.isInt())
        guiScale = juce::jlimit (0.75, 2.0, (double) gs);
}
} // namespace lens::state
