#include "ParamReader.h"

#include "Parameters.h"

namespace lens::params
{
namespace
{
int idx (const std::atomic<float>* p) noexcept { return (int) std::lround (p->load (std::memory_order_relaxed)); }
bool on (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed) >= 0.5f; }
float val (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); }
} // namespace

ParamReader::ParamReader (juce::AudioProcessorValueTreeState& apvts)
{
    auto get = [&apvts] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int b = 0; b < dsp::kNumBands; ++b)
    {
        auto g = [&] (const char* suffix) { return get (bandParamId (b + 1, suffix)); };
        auto& p = bands[(size_t) b];
        p.on = g (band::on);
        p.shape = g (band::shape);
        p.freq = g (band::freq);
        p.gain = g (band::gain);
        p.q = g (band::q);
        p.slope = g (band::slope);
        p.stereo = g (band::stereo);
        p.dynOn = g (band::dynOn);
        p.dynMode = g (band::dynMode);
        p.dynRange = g (band::dynRange);
        p.dynThresh = g (band::dynThresh);
        p.dynRatio = g (band::dynRatio);
        p.dynAttack = g (band::dynAttack);
        p.dynRelease = g (band::dynRelease);
        p.detSource = g (band::detSource);
        p.detCustom = g (band::detCustom);
        p.detHp = g (band::detHp);
        p.detLp = g (band::detLp);
        p.specOn = g (band::specOn);
        p.specThresh = g (band::specThresh);
        p.specAmount = g (band::specAmount);
    }

    phaseMode = get (id::phaseMode);
    lpResolution = get (id::lpResolution);
    character = get (id::character);
    outputGain = get (id::outputGain);
    autoGain = get (id::autoGain);
    delta = get (id::delta);
    bypass = get (id::bypass);
    abSlot = get (id::abSlot);
    lookahead = get (id::lookahead);
}

void ParamReader::readBand (int b, dsp::BandParams& o) const noexcept
{
    const auto& p = bands[(size_t) b];
    o.on = on (p.on);
    o.shape = (dsp::Shape) juce::jlimit (0, 8, idx (p.shape));
    o.freq = val (p.freq);
    o.gain = val (p.gain);
    o.q = val (p.q);
    o.slope = juce::jlimit (0, dsp::kNumSlopes - 1, idx (p.slope));
    o.stereo = (dsp::StereoMode) juce::jlimit (0, 4, idx (p.stereo));
    o.dynOn = on (p.dynOn);
    o.dynMode = idx (p.dynMode) == 1 ? dsp::DynMode::Below : dsp::DynMode::Above;
    o.dynRange = val (p.dynRange);
    o.dynThresh = val (p.dynThresh);
    o.dynRatio = val (p.dynRatio);
    o.dynAttack = val (p.dynAttack);
    o.dynRelease = val (p.dynRelease);
    o.detExternal = idx (p.detSource) == 1;
    o.detCustom = on (p.detCustom);
    o.detHp = val (p.detHp);
    o.detLp = val (p.detLp);
    o.specOn = on (p.specOn);
    o.specThresh = val (p.specThresh);
    o.specAmount = val (p.specAmount);
}

void ParamReader::read (dsp::EngineParams& o) const noexcept
{
    for (int b = 0; b < dsp::kNumBands; ++b)
        readBand (b, o.bands[(size_t) b]);

    auto& g = o.global;
    g.linearPhase = idx (phaseMode) == 1;
    g.lpResolution = (dsp::LpResolution) juce::jlimit (0, 2, idx (lpResolution));
    g.warm = idx (character) == 1;
    g.outputGain = val (outputGain);
    g.autoGain = on (autoGain);
    g.delta = on (delta);
    g.bypass = on (bypass);
    g.abSlot = idx (abSlot);
    g.lookaheadMs = val (lookahead);
}
} // namespace lens::params
