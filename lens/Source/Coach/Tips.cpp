#include "Tips.h"

#include "Util/Format.h"

namespace lens::coach
{
namespace
{
using dsp::Shape;

bool isBassType (const juce::String& t) { return t == "Bass" || t == "Kick & Drums"; }
bool isVocalType (const juce::String& t) { return t == "Vocal" || t == "Choir & Vocal Bus"; }

// Replaces "{a|b}" with option index.
juce::String pick (juce::String text, const juce::String& firstOption, int index)
{
    const int start = text.indexOf ("{" + firstOption + "|");
    if (start < 0)
        return text;
    const int end = text.indexOf (start, "}");
    const auto group = text.substring (start, end + 1);
    const auto parts = juce::StringArray::fromTokens (group.substring (1, group.length() - 1), "|", "");
    return text.replace (group, parts[index]);
}
} // namespace

TipEngine::TipEngine (const Content& c) : content (c) {}

void TipEngine::setNeverShow (const juce::StringArray& ids)
{
    never.clear();
    for (const auto& s : ids)
        never.insert (s);
}

bool TipEngine::hold (const juce::String& key, bool condition, double seconds, double now)
{
    if (! condition)
    {
        since.erase (key);
        return false;
    }
    auto it = since.find (key);
    if (it == since.end())
        it = since.emplace (key, now).first;
    return now - it->second >= seconds;
}

juce::String TipEngine::text (const juce::String& id) const
{
    auto it = firing.find (id);
    return it != firing.end() ? it->second : juce::String();
}

void TipEngine::update (const CoachState& s)
{
    firing.clear();
    const double now = s.time;
    const auto& bands = s.params.bands;
    const auto& g = s.params.global;
    auto t = [this] (const char* id) { return content.tip (id) != nullptr ? content.tip (id)->text : juce::String ("TODO: ") + id; };

    // tip_big_boost: any static boost > +8 dB.
    bool big = false, narrow = false, shelfQ = false, lowcutBass = false, lowcutMaster = false, masterBig = false, lowRelease = false;
    bool sibilance = false, sideBand = false, externalBand = false;
    int numOn = 0;
    for (const auto& b : bands)
    {
        if (! b.on)
            continue;
        ++numOn;
        if (dsp::usesGain (b.shape) && b.gain > 8.0f)
            big = true;
        if (b.shape == Shape::Bell && b.gain > 0.0f && b.q > 6.0f)
            narrow = true;
        if (dsp::isShelf (b.shape) && b.gain > 0.0f && b.q > 1.5f)
            shelfQ = true;
        if (b.shape == Shape::LowCut && b.freq > 45.0f && isBassType (s.trackType))
            lowcutBass = true;
        if (b.shape == Shape::LowCut && s.trackType == "Master" && (b.freq > 35.0f || dsp::slopeDbPerOct (b.slope) > 24 || b.slope == dsp::kSlopeBrickwall))
            lowcutMaster = true;
        if (s.trackType == "Master" && dsp::usesGain (b.shape) && std::abs (b.gain) > 3.0f)
            masterBig = true;
        if (b.isDynamic() && b.freq < 150.0f && b.dynRelease < 50.0f)
            lowRelease = true;
        if (isVocalType (s.trackType) && b.isDynamic() && b.freq >= 4000.0f && b.freq <= 10000.0f && b.dynAttack > 10.0f)
            sibilance = true;
        if (b.stereo == dsp::StereoMode::Side)
            sideBand = true;
        if (b.isDynamic() && b.detExternal)
            externalBand = true;
    }

    // tip_stacked: two boosting bands within 1/3 octave.
    bool stacked = false;
    for (size_t i = 0; i < bands.size() && ! stacked; ++i)
        for (size_t j = i + 1; j < bands.size() && ! stacked; ++j)
        {
            const auto& a = bands[i];
            const auto& b = bands[j];
            if (a.on && b.on && dsp::usesGain (a.shape) && dsp::usesGain (b.shape) && a.gain > 0.0f && b.gain > 0.0f
                && std::abs (std::log2 (a.freq / b.freq)) < 1.0 / 3.0)
                stacked = true;
        }

    if (big)
        fire ("tip_big_boost", t ("tip_big_boost"));
    if (narrow)
        fire ("tip_narrow_boost", t ("tip_narrow_boost"));

    // tip_louder_trap: auto-gain off, output differs from input by > 1.5 LU for 3 s.
    {
        const bool valid = s.inputLufs > -150.0f && s.outputLufs > -150.0f;
        const float diff = s.outputLufs - s.inputLufs;
        if (hold ("tip_louder_trap", ! g.autoGain && valid && std::abs (diff) > 1.5f, 3.0, now))
            fire ("tip_louder_trap", pick (t ("tip_louder_trap"), "louder", diff > 0 ? 0 : 1).replace ("{x}", fmt::plain (diff)));
    }

    if (hold ("tip_solo_long", s.soloBand >= 0 || s.listenBand >= 0, 15.0, now))
        fire ("tip_solo_long", t ("tip_solo_long"));

    // tip_dyn_never / tip_dyn_always, per band.
    for (int b = 0; b < dsp::kNumBands; ++b)
    {
        const auto& bp = bands[(size_t) b];
        const auto key = juce::String (b);
        const bool dyn = bp.isDynamic();
        if (hold ("never#" + key, dyn && s.activity[(size_t) b] < 0.5f && s.playing(), 10.0, now) && ! isFiring ("tip_dyn_never"))
            fire ("tip_dyn_never", t ("tip_dyn_never").replace ("{n}", juce::String (b + 1)));
        if (hold ("always#" + key, dyn && s.activity[(size_t) b] > 90.0f, 10.0, now) && ! isFiring ("tip_dyn_always"))
            fire ("tip_dyn_always", t ("tip_dyn_always").replace ("{n}", juce::String (b + 1)));
    }

    if (lowcutBass)
        fire ("tip_lowcut_bass", t ("tip_lowcut_bass"));
    if (lowcutMaster)
        fire ("tip_lowcut_master", t ("tip_lowcut_master"));
    if (masterBig)
        fire ("tip_master_big", t ("tip_master_big"));

    // tip_clip: output peak above 0 dBFS (stays up for 3 s).
    if (s.outputPeak > 1.0f)
        lastClip = now;
    if (now - lastClip < 3.0)
        fire ("tip_clip", t ("tip_clip"));

    if (g.linearPhase && isBassType (s.trackType))
        fire ("tip_lp_punch", t ("tip_lp_punch"));

    // Once-per-session tips stay until dismissed.
    if (g.linearPhase && ! prevLinearPhase && ! seenLinearPhase)
    {
        seenLinearPhase = true;
        latched.insert ("tip_lp_latency");
    }
    prevLinearPhase = g.linearPhase;
    if (latched.count ("tip_lp_latency"))
        fire ("tip_lp_latency", t ("tip_lp_latency").replace ("{ms}", juce::String (s.latencyMs, 1)));

    if (lowRelease)
        fire ("tip_low_release", t ("tip_low_release"));
    if (hold ("tip_sc_missing", externalBand && ! s.sidechainPresent() && s.playing(), 5.0, now))
        fire ("tip_sc_missing", t ("tip_sc_missing"));
    if (numOn > 10)
        fire ("tip_many_bands", t ("tip_many_bands"));
    if (stacked)
        fire ("tip_stacked", t ("tip_stacked"));
    if (sibilance)
        fire ("tip_sibilance_attack", t ("tip_sibilance_attack"));
    if (hold ("tip_mono_side", sideBand && s.numChannels > 1 && s.sideLevelDb < s.midLevelDb - 40.0f && s.midLevelDb > -150.0f, 5.0, now))
        fire ("tip_mono_side", t ("tip_mono_side"));
    if (shelfQ)
        fire ("tip_shelf_q", t ("tip_shelf_q"));

    const int dynCount = s.dynamicBands();
    if (dynCount > 0 && prevDynamicCount == 0 && ! seenDynamic)
    {
        seenDynamic = true;
        latched.insert ("tip_try_delta");
    }
    prevDynamicCount = dynCount;
    if (latched.count ("tip_try_delta"))
        fire ("tip_try_delta", t ("tip_try_delta"));

    if (s.trackType == "Master" && ! seenMaster)
    {
        seenMaster = true;
        latched.insert ("tip_master_order");
    }
    if (latched.count ("tip_master_order"))
        fire ("tip_master_order", t ("tip_master_order"));
}

std::vector<TipEngine::ActiveTip> TipEngine::visible (int max) const
{
    std::vector<ActiveTip> out;
    if (quiet)
        return out;
    for (const auto& tip : content.tips)
    {
        if ((int) out.size() >= max)
            break;
        auto it = firing.find (tip.id);
        if (it == firing.end() || hiddenThisSession.count (tip.id) || never.count (tip.id))
            continue;
        out.push_back ({ tip.id, it->second });
    }
    return out;
}
} // namespace lens::coach
