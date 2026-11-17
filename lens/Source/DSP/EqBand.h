#pragma once

#include "FilterDesign.h"
#include "Smoothers.h"
#include "Svf.h"
#include "Types.h"

namespace lens::dsp
{
// A configured filter cascade with per-channel state and a stereo mode.
class BandFilter
{
public:
    void setTopology (StereoMode mode, int numChannels) noexcept;
    void setDesign (const BandDesign&) noexcept;
    // Replaces section 0 (the gain section of a Bell or shelf).
    void setSection0 (const SvfSection&) noexcept;
    void reset() noexcept;
    bool isIdentity() const noexcept { return identity; }

    // In-place over n samples.
    void process (double* L, double* R, int n) noexcept;
    void processSample (double& L, double& R) noexcept;

private:
    double tickChannel (int ch, double x) noexcept
    {
        for (int i = 0; i < numSvf; ++i)
            x = svfTick (svf[(size_t) i], st[ch][(size_t) i], x);
        if (hasOnePole)
            x = onePoleTick (onePole, opState[ch], x);
        return x;
    }

    StereoMode mode = StereoMode::Stereo;
    int channels = 2;
    bool identity = true;
    int numSvf = 0;
    std::array<SvfRun, kMaxSvfSections> svf {};
    bool hasOnePole = false;
    OnePoleRun onePole {};
    std::array<SvfState, kMaxSvfSections> st[2] {};
    double opState[2] {};
};

// One band slot: smoothing, design updates every 8 samples, per-sample
// dynamic gain, and a 10 ms crossfade whenever the band's on state,
// shape, slope or stereo mode changes.
class EqBand
{
public:
    static constexpr int tickInterval = 8;

    void prepare (double sampleRate, int numChannels);

    // Called once per block with the latest parameters. With snap, the
    // band jumps to them without smoothing or crossfade.
    void setParams (const BandParams&, bool snap) noexcept;

    // Called at every 8-sample tick (on the engine's sample counter).
    void tick() noexcept;

    // True while the band changes the sound (on, or fading).
    bool isActive() const noexcept { return config.on || fadeLeft > 0; }

    // In-place. dynDb (n values, may be null) is the dynamic gain to add to
    // the static gain, for Bell and shelf bands.
    void process (double* L, double* R, int n, const double* dynDb) noexcept;

    // Smoothed values (for the solo filter and auto-gain).
    double smoothedFreq() const noexcept;
    double smoothedGain() const noexcept { return gainDb.current; }
    double smoothedQ() const noexcept;
    const BandDesign& design() const noexcept { return currentDesign; }
    const BandParams& params() const noexcept { return target; }
    bool isOn() const noexcept { return config.on; }

private:
    struct Config
    {
        bool on = false;
        Shape shape = Shape::Bell;
        int slope = 1;
        StereoMode mode = StereoMode::Stereo;

        bool operator== (const Config& o) const noexcept
        {
            return on == o.on && shape == o.shape && slope == o.slope && mode == o.mode;
        }
        bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
    };

    static Config configOf (const BandParams&) noexcept;
    void applyConfig (BandFilter&, const Config&) noexcept;
    void redesign() noexcept;

    double fs = 48000.0;
    int channels = 2;
    int smoothLen = 960;
    int fadeLen = 480;

    BandParams target {};
    Config wanted {}, config {};
    Ramp logFreq, gainDb, logQ;
    BandDesign currentDesign {};
    double lastDynTotal = 1.0e9;
    double designedFreq = 0.0, designedGain = 0.0, designedQ = 0.0;
    Config designedConfig {};
    bool designed = false;

    BandFilter cur, prev;
    int fadeLeft = 0;
    bool firstTick = true;
};
} // namespace lens::dsp
