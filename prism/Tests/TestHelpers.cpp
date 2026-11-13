#include "TestHelpers.h"
#include "DSP/FFT.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace prism::test
{
std::unique_ptr<PrismProcessor> makeProcessor (double sr, int maxBlock)
{
    auto p = std::make_unique<PrismProcessor>();
    p->setNonRealtime (true);
    p->setPlayConfigDetails (0, 2, sr, maxBlock);
    p->prepareToPlay (sr, maxBlock);
    waitForTables (*p);
    return p;
}

void setParam (PrismProcessor& p, const std::string& id, float v)
{
    const int idx = paramIndex (id);
    jassert (idx >= 0);
    if (idx < 0) return;
    auto* prm = p.param (idx);
    prm->setValueNotifyingHost (prm->convertTo0to1 (paramDefs()[(size_t) idx].snap (v)));
}

float getParam (PrismProcessor& p, const std::string& id)
{
    const int idx = paramIndex (id);
    auto* prm = p.param (idx);
    return prm->convertFrom0to1 (prm->getValue());
}

void waitForTables (PrismProcessor& p)
{
    p.tables.waitUntilGenerated (0, 20000);
    p.tables.waitUntilGenerated (1, 20000);
}

void loadPresetNow (PrismProcessor& p, const PresetData& d)
{
    p.loadPreset (d, false);
    waitForTables (p);
}

juce::AudioBuffer<float> render (PrismProcessor& p, const std::vector<Note>& notes, double seconds,
                                 const RenderOptions& opt, const std::vector<MidiEvent>& extra)
{
    const double sr = opt.sampleRate;
    const int total = (int) std::llround (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    out.clear();

    // build a time-sorted list of events (sample positions)
    struct Ev { int64_t pos; juce::MidiMessage m; };
    std::vector<Ev> evs;
    for (auto& n : notes)
    {
        evs.push_back ({ (int64_t) std::llround (n.start * sr), juce::MidiMessage::noteOn (n.channel, n.note, n.velocity) });
        evs.push_back ({ (int64_t) std::llround ((n.start + n.length) * sr), juce::MidiMessage::noteOff (n.channel, n.note, 0.5f) });
    }
    for (auto& e : extra) evs.push_back ({ (int64_t) std::llround (e.time * sr), e.msg });
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos; });

    p.setNonRealtime (opt.nonRealtime);
    if (opt.transport != nullptr) { opt.transport->sr = sr; p.setPlayHead (opt.transport); }
    else p.setPlayHead (nullptr);

    juce::MidiBuffer midi;
    size_t ei = 0;
    int64_t pos = 0;
    size_t bi = 0;
    while (pos < total)
    {
        const int bs = std::min<int64_t> (opt.blocks[bi % opt.blocks.size()], total - pos);
        ++bi;
        if (opt.beforeBlock) opt.beforeBlock (p, pos);
        if (opt.transport != nullptr) opt.transport->samplePos = pos;
        midi.clear();
        while (ei < evs.size() && evs[ei].pos < pos + bs)
        {
            midi.addEvent (evs[ei].m, (int) std::max<int64_t> (0, evs[ei].pos - pos));
            ++ei;
        }
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, (int) pos, bs);
        p.processBlock (block, midi);
        if (opt.callService) { p.servicePresetLoad(); p.collectGarbage(); }
        pos += bs;
    }
    p.setPlayHead (nullptr);
    return out;
}

double rms (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    if (len < 0) len = b.getNumSamples() - start;
    double s = 0;
    const float* x = b.getReadPointer (ch);
    for (int i = start; i < start + len; ++i) s += (double) x[i] * x[i];
    return len > 0 ? std::sqrt (s / len) : 0.0;
}

double peak (const juce::AudioBuffer<float>& b, int start, int len)
{
    if (len < 0) len = b.getNumSamples() - start;
    double m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        for (int i = start; i < start + len; ++i) m = std::max (m, (double) std::abs (x[i]));
    }
    return m;
}

double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double m = 0;
    const int n = std::min (a.getNumSamples(), b.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i) m = std::max (m, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return m;
}

double maxDelta (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    if (len < 0) len = b.getNumSamples() - start;
    const float* x = b.getReadPointer (ch);
    double m = 0;
    for (int i = std::max (1, start); i < start + len; ++i) m = std::max (m, (double) std::abs (x[i] - x[i - 1]));
    return m;
}

double estimateFrequency (const float* x, int n, double sr)
{
    // remove DC
    double mean = 0;
    for (int i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    double first = -1, last = -1;
    int count = 0;
    for (int i = 1; i < n; ++i)
    {
        const double a = x[i - 1] - mean, b = x[i] - mean;
        if (a < 0 && b >= 0)
        {
            const double t = (i - 1) + (-a / (b - a));
            if (first < 0) first = t; else ++count;
            last = t;
        }
    }
    if (count <= 0) return 0.0;
    return sr * count / (last - first);
}

std::vector<double> spectrumDb (const float* x, int n)
{
    auto w = dsp::blackmanHarris (n);
    std::vector<dsp::cplx> a ((size_t) n);
    double wsum = 0;
    for (int i = 0; i < n; ++i) { a[(size_t) i] = dsp::cplx (x[i] * w[(size_t) i], 0); wsum += w[(size_t) i]; }
    dsp::fft (a, false);
    std::vector<double> db ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
        db[(size_t) k] = 20.0 * std::log10 (std::abs (a[(size_t) k]) * 2.0 / wsum + 1e-15);
    return db;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace prism::test
