#include "StateIO.h"

#include "Params/Parameters.h"

#include <cmath>

namespace lens::state
{
namespace
{
struct BandKey
{
    const char* group; // "" for top-level, else "dyn" / "det" / "spec"
    const char* key;
    const char* suffix;
};

namespace pb = lens::params::band;

const BandKey bandKeys[] = {
    { "", "on", pb::on },
    { "", "shape", pb::shape },
    { "", "freq", pb::freq },
    { "", "gain", pb::gain },
    { "", "q", pb::q },
    { "", "slope", pb::slope },
    { "", "stereo", pb::stereo },
    { "dyn", "on", pb::dynOn },
    { "dyn", "mode", pb::dynMode },
    { "dyn", "range", pb::dynRange },
    { "dyn", "thresh", pb::dynThresh },
    { "dyn", "ratio", pb::dynRatio },
    { "dyn", "attack", pb::dynAttack },
    { "dyn", "release", pb::dynRelease },
    { "det", "source", pb::detSource },
    { "det", "custom", pb::detCustom },
    { "det", "hp", pb::detHp },
    { "det", "lp", pb::detLp },
    { "spec", "on", pb::specOn },
    { "spec", "thresh", pb::specThresh },
    { "spec", "amount", pb::specAmount },
};

// roundFloats: presets store 6 significant digits, so files are readable
// and save → load → save gives identical text despite log-range rounding
// noise. Plugin state keeps full precision so sessions reload exactly.
juce::var paramToVar (const juce::RangedAudioParameter& p, bool roundFloats)
{
    if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
        return params::choiceToken (*c, c->getIndex());
    if (auto* b = dynamic_cast<const juce::AudioParameterBool*> (&p))
        return b->get();
    if (auto* f = dynamic_cast<const juce::AudioParameterFloat*> (&p))
    {
        const double v = (double) f->get();
        if (! roundFloats)
            return v;
        if (juce::exactlyEqual (v, 0.0) || ! std::isfinite (v))
            return 0.0;
        const double scale = std::pow (10.0, 5 - (int) std::floor (std::log10 (std::abs (v))));
        return std::round (v * scale) / scale;
    }
    jassertfalse;
    return {};
}

void setNormalised (juce::RangedAudioParameter& p, float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    // Values that only differ by float rounding through a log range are
    // left alone, so re-applying a setup (undo, A/B) restores it exactly.
    if (std::abs (p.getValue() - n) > 1.0e-6f)
        p.setValueNotifyingHost (n);
}

// The normalised value a JSON value asks for, or false if it is unusable.
bool normalisedFor (const juce::RangedAudioParameter& p, const juce::var& v, float& n)
{
    if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
    {
        if (! v.isString())
            return false;
        const int idx = params::choiceIndexForToken (*c, v.toString());
        if (idx < 0)
            return false;
        n = p.convertTo0to1 ((float) idx);
        return true;
    }
    if (dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr)
    {
        if (! v.isBool())
            return false;
        n = (bool) v ? 1.0f : 0.0f;
        return true;
    }
    if (v.isDouble() || v.isInt() || v.isInt64())
    {
        const auto d = (double) v;
        if (! std::isfinite (d))
            return false;
        const auto& r = p.getNormalisableRange();
        n = p.convertTo0to1 (juce::jlimit (r.start, r.end, (float) d));
        return true;
    }
    return false;
}

void varToParam (juce::RangedAudioParameter& p, const juce::var& v)
{
    float n;
    if (normalisedFor (p, v, n))
        setNormalised (p, n);
}

void resetToDefault (juce::RangedAudioParameter& p)
{
    setNormalised (p, p.getDefaultValue());
}

juce::DynamicObject* childObject (juce::DynamicObject& parent, const char* name)
{
    if (auto* existing = parent.getProperty (name).getDynamicObject())
        return existing;
    auto* o = new juce::DynamicObject();
    parent.setProperty (name, juce::var (o));
    return o;
}
} // namespace

juce::var bandsToVar (const APVTS& apvts, bool onlyEnabled)
{
    juce::Array<juce::var> bands;

    for (int b = 1; b <= params::numBands; ++b)
    {
        if (onlyEnabled && apvts.getRawParameterValue (params::bandParamId (b, pb::on))->load() < 0.5f)
            continue;

        auto* obj = new juce::DynamicObject();
        juce::var holder (obj);
        obj->setProperty ("slot", b);

        for (const auto& k : bandKeys)
        {
            auto* p = apvts.getParameter (params::bandParamId (b, k.suffix));
            jassert (p != nullptr);
            auto* target = k.group[0] == 0 ? obj : childObject (*obj, k.group);
            target->setProperty (k.key, paramToVar (*p, onlyEnabled));
        }

        bands.add (holder);
    }

    return bands;
}

void applyBands (APVTS& apvts, const juce::var& bandsArray)
{
    // The entry for each slot (later entries win).
    std::array<juce::var, params::numBands + 1> entries;
    if (const auto* arr = bandsArray.getArray())
        for (const auto& entry : *arr)
        {
            if (! entry.isObject() || ! (entry["slot"].isInt() || entry["slot"].isInt64() || entry["slot"].isDouble()))
                continue;
            const int b = (int) entry["slot"];
            if (b >= 1 && b <= params::numBands)
                entries[(size_t) b] = entry;
        }

    // Each parameter is set once, straight to its final value (listed
    // value, else default), so nothing drifts through a reset.
    for (int b = 1; b <= params::numBands; ++b)
    {
        const auto& entry = entries[(size_t) b];
        for (const auto& k : bandKeys)
        {
            auto* p = apvts.getParameter (params::bandParamId (b, k.suffix));
            const auto& src = k.group[0] == 0 ? entry : entry[k.group];
            float n;
            if (entry.isObject() && src.isObject() && src.hasProperty (k.key) && normalisedFor (*p, src[k.key], n))
                setNormalised (*p, n);
            else if (entry.isObject() && juce::String (k.suffix) == pb::on)
                setNormalised (*p, 1.0f); // a listed band is on unless it says otherwise
            else
                setNormalised (*p, p->getDefaultValue());
        }
    }
}

juce::var globalsToVar (const APVTS& apvts, bool forPreset)
{
    auto* obj = new juce::DynamicObject();
    juce::var holder (obj);

    for (const auto& gid : params::globalIds())
    {
        if (forPreset && (gid == params::id::bypass || gid == params::id::abSlot))
            continue;
        obj->setProperty (gid, paramToVar (*apvts.getParameter (gid), forPreset));
    }

    return holder;
}

void applyGlobals (APVTS& apvts, const juce::var& globals, bool forPreset)
{
    if (! globals.isObject())
        return;

    for (const auto& gid : params::globalIds())
    {
        if (forPreset && (gid == params::id::bypass || gid == params::id::abSlot))
            continue;
        if (globals.hasProperty (gid))
            varToParam (*apvts.getParameter (gid), globals[juce::Identifier (gid)]);
    }
}

bool migrate (juce::var& root)
{
    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return false;

    int version = 1;
    const auto& v = obj->getProperty ("format_version");
    if (v.isInt() || v.isInt64() || v.isDouble())
        version = juce::jmax (1, (int) v);

    // Migration chain. Each step upgrades the document by exactly one
    // version; add `case N: migrateNtoNplus1 (*obj); break;` here when
    // the format changes. Documents from a newer LENS are loaded as-is,
    // best effort (unknown keys are ignored by the readers).
    while (version < currentFormatVersion)
    {
        switch (version)
        {
            default: break;
        }
        ++version;
    }

    obj->setProperty ("format_version", juce::jmax (version, currentFormatVersion));
    return true;
}
} // namespace lens::state
