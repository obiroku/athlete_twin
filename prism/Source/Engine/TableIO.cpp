#include "TableIO.h"
#include "../DSP/Wavetable.h"
#include <cmath>

namespace prism
{
namespace
{
constexpr int N = dsp::Wavetable::N;

// Resample one cycle (any length, fractional allowed) to N samples with cubic interpolation.
std::vector<float> resampleCycle (const std::vector<float>& x, double start, double len)
{
    std::vector<float> out ((size_t) N);
    const int sz = (int) x.size();
    auto at = [&] (int i) { i = std::max (0, std::min (sz - 1, i)); return x[(size_t) i]; };
    for (int i = 0; i < N; ++i)
    {
        const double pos = start + len * (double) i / (double) N;
        const int i1 = (int) std::floor (pos);
        const float f = (float) (pos - i1);
        const float xm1 = at (i1 - 1), x0 = at (i1), x1 = at (i1 + 1), x2 = at (i1 + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        out[(size_t) i] = ((c3 * f + c2) * f + c1) * f + x0;
    }
    return out;
}
} // namespace

int readClmFrameSize (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return 0;
    char id[4];
    if (in.read (id, 4) != 4 || std::memcmp (id, "RIFF", 4) != 0) return 0;
    in.readInt();
    if (in.read (id, 4) != 4 || std::memcmp (id, "WAVE", 4) != 0) return 0;
    while (! in.isExhausted())
    {
        if (in.read (id, 4) != 4) break;
        const int size = in.readInt();
        if (size < 0) break;
        if (std::memcmp (id, "clm ", 4) == 0)
        {
            juce::MemoryBlock mb;
            in.readIntoMemoryBlock (mb, size);
            const auto s = mb.toString();
            const int p = s.indexOf ("<!>");
            if (p >= 0) return std::max (0, s.substring (p + 3).getIntValue());
            return 0;
        }
        in.skipNextBytes (size + (size & 1));
    }
    return 0;
}

double detectCycleLength (const std::vector<float>& x)
{
    // normalised autocorrelation on (up to) the first 16384 samples
    const int n = (int) std::min<size_t> (x.size(), 16384);
    if (n < 64) return 0.0;
    const int maxLag = n / 2;
    std::vector<double> r ((size_t) maxLag + 1, 0.0);
    double e0 = 0;
    for (int i = 0; i < n; ++i) e0 += (double) x[(size_t) i] * x[(size_t) i];
    if (e0 <= 1e-12) return 0.0;
    for (int lag = 16; lag <= maxLag; ++lag)
    {
        double s = 0, ea = 0, eb = 0;
        for (int i = 0; i + lag < n; ++i)
        {
            s += (double) x[(size_t) i] * x[(size_t) i + lag];
            ea += (double) x[(size_t) i] * x[(size_t) i];
            eb += (double) x[(size_t) i + lag] * x[(size_t) i + lag];
        }
        r[(size_t) lag] = s / std::sqrt (std::max (1e-20, ea * eb));
    }
    // first peak above 0.8 after the correlation dips below zero
    int lag = 16;
    while (lag < maxLag && r[(size_t) lag] > 0.0) ++lag;
    int best = 0;
    for (; lag < maxLag; ++lag)
        if (r[(size_t) lag] > 0.8 && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { best = lag; break; }
    if (best == 0) return 0.0;
    // parabolic refinement
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double d = a - 2 * b + c;
    return best + (std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0);
}

ImportResult importFromSamples (const std::vector<float>& mono, ImportMode mode, int fixedFrames, int clm)
{
    ImportResult r;
    r.clmFrameSize = clm;
    if (mono.empty()) { r.error = "empty file"; return r; }
    const int len = (int) mono.size();
    if (mode == ImportMode::Auto)
    {
        if (clm > 0) mode = ImportMode::Fixed;
        else if (len <= 4096) mode = ImportMode::SingleCycle;
        else if (len % N == 0) mode = ImportMode::Fixed;
        else mode = ImportMode::PitchDetect;
    }
    std::vector<std::vector<float>> frames;
    switch (mode)
    {
        case ImportMode::SingleCycle:
            frames.push_back (resampleCycle (mono, 0.0, (double) len));
            break;
        case ImportMode::Fixed:
        {
            int count;
            double flen;
            if (clm > 0) { count = std::max (1, len / clm); flen = clm; }
            else if (fixedFrames <= 0 && len % N == 0) { count = len / N; flen = N; }
            else { count = std::max (1, fixedFrames); flen = (double) len / count; }
            count = std::min (count, dsp::Wavetable::MaxFrames * 64);
            // more than 256 cycles: pick evenly spaced ones
            const int use = std::min (count, dsp::Wavetable::MaxFrames);
            for (int i = 0; i < use; ++i)
            {
                const int src = use > 1 ? (int) std::lround ((double) i * (count - 1) / (use - 1)) : 0;
                frames.push_back (resampleCycle (mono, src * flen, flen));
            }
            break;
        }
        case ImportMode::PitchDetect:
        {
            const double cyc = detectCycleLength (mono);
            if (cyc <= 0.0) { r.error = "no stable pitch found"; return r; }
            const int count = std::max (1, (int) std::floor ((double) len / cyc));
            const int use = std::min (count, dsp::Wavetable::MaxFrames);
            for (int i = 0; i < use; ++i)
            {
                const int src = use > 1 ? (int) std::lround ((double) i * (count - 1) / (use - 1)) : 0;
                frames.push_back (resampleCycle (mono, src * cyc, cyc));
            }
            break;
        }
        default: break;
    }
    dsp::applyTableHygiene (frames);
    r.frames = std::move (frames);
    r.ok = ! r.frames.empty();
    return r;
}

ImportResult importWavetable (const juce::File& f, ImportMode mode, int fixedFrames)
{
    ImportResult r;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) { r.error = "unsupported or unreadable file"; return r; }
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, 256 * 8192);
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    r = importFromSamples (mono, mode, fixedFrames, readClmFrameSize (f));
    r.name = f.getFileNameWithoutExtension();
    return r;
}

bool exportWavetable (const juce::File& f, const std::vector<std::vector<float>>& frames, double sampleRate)
{
    juce::MemoryOutputStream data;
    for (auto& fr : frames)
        for (int i = 0; i < N; ++i) data.writeFloat (i < (int) fr.size() ? fr[(size_t) i] : 0.0f);
    juce::String clm = "<!>" + juce::String (N) + " 00000000 PRISM";
    juce::MemoryBlock clmBytes (clm.toRawUTF8(), clm.getNumBytesAsUTF8());
    if (clmBytes.getSize() & 1) clmBytes.append ("\0", 1);

    juce::MemoryOutputStream out;
    const int fmtSize = 16;
    const int riffSize = 4 + (8 + fmtSize) + (8 + (int) clmBytes.getSize()) + (8 + (int) data.getDataSize());
    out.write ("RIFF", 4); out.writeInt (riffSize); out.write ("WAVE", 4);
    out.write ("fmt ", 4); out.writeInt (fmtSize);
    out.writeShort (3); // IEEE float
    out.writeShort (1);
    out.writeInt ((int) sampleRate);
    out.writeInt ((int) sampleRate * 4);
    out.writeShort (4);
    out.writeShort (32);
    out.write ("clm ", 4); out.writeInt ((int) clmBytes.getSize()); out.write (clmBytes.getData(), clmBytes.getSize());
    out.write ("data", 4); out.writeInt ((int) data.getDataSize()); out.write (data.getData(), data.getDataSize());
    f.getParentDirectory().createDirectory();
    return f.replaceWithData (out.getData(), out.getDataSize());
}
} // namespace prism
