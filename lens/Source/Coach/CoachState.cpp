#include "CoachState.h"

#include "PluginProcessor.h"

namespace lens::coach
{
int CoachState::bandsOn() const noexcept
{
    int n = 0;
    for (const auto& b : params.bands)
        n += b.on ? 1 : 0;
    return n;
}

int CoachState::dynamicBands() const noexcept
{
    int n = 0;
    for (const auto& b : params.bands)
        n += b.isDynamic() ? 1 : 0;
    return n;
}

CoachState sampleProcessor (LensProcessor& p, double t)
{
    CoachState s;
    s.time = t;
    p.readParams (s.params);
    s.trackType = p.getSession().trackType;
    s.numChannels = p.isMono() ? 1 : 2;
    auto& st = p.stats();
    for (int b = 0; b < dsp::kNumBands; ++b)
        s.activity[(size_t) b] = st.bands[(size_t) b].activity.load();
    s.inputLufs = st.inputLufs.load();
    s.outputLufs = st.outputLufs.load();
    s.outputPeak = st.outputPeakHold.exchange (0.0f);
    s.inputLevelDb = st.inputLevelDb.load();
    s.midLevelDb = st.midLevelDb.load();
    s.sideLevelDb = st.sideLevelDb.load();
    s.sidechainLevelDb = st.sidechainLevelDb.load();
    s.soloBand = p.soloBand.load();
    s.listenBand = p.listenBand.load();
    const double fs = p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0;
    s.latencyMs = (float) (1000.0 * st.latencySamples.load() / fs);
    return s;
}
} // namespace lens::coach
