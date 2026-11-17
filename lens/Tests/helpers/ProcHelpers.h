#pragma once

#include "Params/Parameters.h"
#include "PluginProcessor.h"

namespace lens::test
{
inline void setParam (lens::LensProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getApvts().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

inline float getParam (lens::LensProcessor& p, const juce::String& id)
{
    return p.getApvts().getRawParameterValue (id)->load();
}

inline void setBandParam (lens::LensProcessor& p, int band, const char* suffix, float value)
{
    setParam (p, lens::params::bandParamId (band, suffix), value);
}

struct BandSpec
{
    int shape = 0; // dsp::Shape index
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    int slope = 1;
    int stereo = 0;
};

inline void setBand (lens::LensProcessor& p, int band, const BandSpec& s)
{
    namespace pb = lens::params::band;
    setBandParam (p, band, pb::shape, (float) s.shape);
    setBandParam (p, band, pb::freq, s.freq);
    setBandParam (p, band, pb::gain, s.gain);
    setBandParam (p, band, pb::q, s.q);
    setBandParam (p, band, pb::slope, (float) s.slope);
    setBandParam (p, band, pb::stereo, (float) s.stereo);
    setBandParam (p, band, pb::on, 1.0f);
}

struct DynSpec
{
    int mode = 0; // 0 Above, 1 Below
    float range = -6.0f, thresh = -20.0f, ratio = 3.0f, attack = 10.0f, release = 120.0f;
    bool external = false, custom = false;
    float hp = 20.0f, lp = 20000.0f;
};

inline void setDyn (lens::LensProcessor& p, int band, const DynSpec& d)
{
    namespace pb = lens::params::band;
    setBandParam (p, band, pb::dynMode, (float) d.mode);
    setBandParam (p, band, pb::dynRange, d.range);
    setBandParam (p, band, pb::dynThresh, d.thresh);
    setBandParam (p, band, pb::dynRatio, d.ratio);
    setBandParam (p, band, pb::dynAttack, d.attack);
    setBandParam (p, band, pb::dynRelease, d.release);
    setBandParam (p, band, pb::detSource, d.external ? 1.0f : 0.0f);
    setBandParam (p, band, pb::detCustom, d.custom ? 1.0f : 0.0f);
    setBandParam (p, band, pb::detHp, d.hp);
    setBandParam (p, band, pb::detLp, d.lp);
    setBandParam (p, band, pb::dynOn, 1.0f);
}

// Eight bands of mixed shapes and stereo modes (block-size and benchmark
// scenarios).
inline void setEightMixedBands (lens::LensProcessor& p, bool withStereoModes = true)
{
    setBand (p, 1, { 3, 35.0f, 0.0f, 1.0f, 3, 0 });                                // Low Cut 24 dB
    setBand (p, 2, { 1, 120.0f, 2.5f, 0.7f, 1, withStereoModes ? 1 : 0 });           // Low Shelf, Mid
    setBand (p, 3, { 0, 300.0f, -3.0f, 1.4f, 1, 0 });                               // Bell
    setBand (p, 4, { 0, 900.0f, 2.0f, 2.0f, 1, withStereoModes ? 3 : 0 });          // Bell, Left
    setBand (p, 5, { 5, 2500.0f, 0.0f, 8.0f, 1, 0 });                               // Notch
    setBand (p, 6, { 0, 4000.0f, -2.0f, 3.0f, 1, withStereoModes ? 2 : 0 });         // Bell, Side
    setBand (p, 7, { 2, 9000.0f, 3.0f, 0.7f, 1, 0 });                               // High Shelf
    setBand (p, 8, { 4, 18000.0f, 0.0f, 1.0f, 5, withStereoModes ? 4 : 0 });         // High Cut 48, Right
}
} // namespace lens::test
