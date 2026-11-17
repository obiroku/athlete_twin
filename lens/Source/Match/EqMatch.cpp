#include "EqMatch.h"

#include "DSP/FilterDesign.h"

namespace lens::match
{
std::vector<double> sixthOctavePoints()
{
    std::vector<double> f;
    for (double x = 20.0; x <= 20000.0 * 1.001; x *= std::pow (2.0, 1.0 / 6.0))
        f.push_back (x);
    return f;
}

juce::var Spectrum::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("seconds", seconds);
    juce::Array<juce::var> f, d;
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        f.add (freqs[i]);
        d.add (db[i]);
    }
    o->setProperty ("freqs", f);
    o->setProperty ("db", d);
    return juce::var (o);
}

Spectrum Spectrum::fromVar (const juce::var& v)
{
    Spectrum s;
    s.name = v["name"].toString();
    s.seconds = (double) v["seconds"];
    if (auto* f = v["freqs"].getArray())
        for (const auto& x : *f)
            s.freqs.push_back ((double) x);
    if (auto* d = v["db"].getArray())
        for (const auto& x : *d)
            s.db.push_back ((double) x);
    if (s.freqs.size() != s.db.size())
        s = {};
    return s;
}

// ---------------------------------------------------------------- LTAS

void LtasAccumulator::prepare (double sampleRate)
{
    fs = sampleRate;
    const int scale = fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
    size = 8192 * scale;
    hop = size / 2;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (size)));
    window.resize ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));
    ring.assign ((size_t) size, 0.0f);
    work.assign ((size_t) (2 * size), 0.0f);
    clear();
}

void LtasAccumulator::clear()
{
    sum.assign ((size_t) (size / 2 + 1), 0.0);
    frames = fed = 0;
    fill = 0;
    writePos = 0;
    std::fill (ring.begin(), ring.end(), 0.0f);
}

void LtasAccumulator::add (const float* x, int n)
{
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) writePos] = x[i];
        writePos = (writePos + 1) % size;
        if (++fill >= hop && fed + i + 1 >= size)
        {
            fill = 0;
            frame();
        }
    }
    fed += n;
}

void LtasAccumulator::frame()
{
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < size; ++i)
        work[(size_t) i] = ring[(size_t) ((writePos + i) % size)] * window[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (work.data(), true);
    for (size_t k = 0; k < sum.size(); ++k)
        sum[k] += (double) work[k] * work[k];
    ++frames;
}

Spectrum LtasAccumulator::result (const juce::String& name) const
{
    Spectrum s;
    s.name = name;
    s.seconds = seconds();
    if (frames == 0)
        return s;
    const double binHz = fs / size;
    s.freqs = sixthOctavePoints();
    for (double f : s.freqs)
    {
        // Mean power over the 1/6-octave band around f.
        const int lo = std::max (1, (int) std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz));
        const int hi = std::min ((int) sum.size() - 1, std::max (lo, (int) std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)));
        double p = 0.0;
        for (int k = lo; k <= hi; ++k)
            p += sum[(size_t) k];
        p /= (double) (hi - lo + 1) * (double) frames;
        s.db.push_back (10.0 * std::log10 (p + 1.0e-30));
    }
    return s;
}

Spectrum analyseFile (const juce::File& file, const std::function<bool()>& shouldStop)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return {};
    LtasAccumulator acc;
    acc.prepare (reader->sampleRate);
    const int chunk = 32768;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, chunk);
    std::vector<float> mono ((size_t) chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        if (shouldStop && shouldStop())
            return {};
        const int n = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - pos);
        reader->read (&buf, 0, n, pos, true, true);
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                m += buf.getSample (ch, i);
            mono[(size_t) i] = m / (float) std::max (1, buf.getNumChannels());
        }
        acc.add (mono.data(), n);
    }
    return acc.result (file.getFileNameWithoutExtension());
}

// ---------------------------------------------------------------- matching

Difference difference (const Spectrum& ref, const Spectrum& mine)
{
    Difference d;
    if (! ref.isValid() || ! mine.isValid() || ref.db.size() != mine.db.size())
        return d;
    // Level-match on 100 Hz – 10 kHz.
    double mr = 0.0, mm = 0.0;
    int n = 0;
    for (size_t i = 0; i < ref.freqs.size(); ++i)
        if (ref.freqs[i] >= 100.0 && ref.freqs[i] <= 10000.0)
        {
            mr += ref.db[i];
            mm += mine.db[i];
            ++n;
        }
    const double offset = n > 0 ? (mr - mm) / n : 0.0;
    std::vector<double> raw (ref.db.size());
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = ref.db[i] - mine.db[i] - offset;
    // 1/3 octave smoothing: three neighbouring 1/6-octave points.
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const double f = ref.freqs[i];
        if (f < 30.0 || f > 16000.0)
            continue;
        const size_t a = i > 0 ? i - 1 : i, b = std::min (raw.size() - 1, i + 1);
        d.freqs.push_back (f);
        d.db.push_back ((0.5 * raw[a] + raw[i] + 0.5 * raw[b]) / 2.0);
    }
    return d;
}

std::vector<Suggestion> suggest (const Difference& diff)
{
    std::vector<Suggestion> out;
    if (diff.db.empty())
        return out;
    auto residual = diff.db;
    for (int k = 0; k < 6; ++k)
    {
        size_t best = 0;
        for (size_t i = 0; i < residual.size(); ++i)
            if (std::abs (residual[i]) > std::abs (residual[best]))
                best = i;
        const double peak = residual[best];
        if (std::abs (peak) < 1.0)
            break;
        // Width where the residual falls to half of the peak.
        size_t l = best, r = best;
        while (l > 0 && residual[l - 1] * peak > 0.0 && std::abs (residual[l - 1]) > 0.5 * std::abs (peak))
            --l;
        while (r + 1 < residual.size() && residual[r + 1] * peak > 0.0 && std::abs (residual[r + 1]) > 0.5 * std::abs (peak))
            ++r;
        const double f = diff.freqs[best];
        const double width = std::max (diff.freqs[r] * std::pow (2.0, 1.0 / 12.0) - diff.freqs[l] * std::pow (2.0, -1.0 / 12.0), f * 0.1);
        Suggestion s;
        s.freq = (float) f;
        s.gainDb = (float) juce::jlimit (-6.0, 6.0, peak);
        s.q = (float) juce::jlimit (0.3, 2.0, f / width);
        out.push_back (s);
        for (size_t i = 0; i < residual.size(); ++i)
            residual[i] -= dsp::toDb (std::abs (dsp::analogPrototype (dsp::Shape::Bell, s.freq, s.gainDb, s.q, 1, diff.freqs[i])));
    }
    return out;
}

juce::File referencesFolder()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("LENS_REFERENCE_DIR", {});
    if (overridePath.isNotEmpty())
        return juce::File (overridePath);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LENS/References");
}

bool saveReference (const Spectrum& s)
{
    const auto dir = referencesFolder();
    dir.createDirectory();
    return dir.getChildFile (juce::File::createLegalFileName (s.name) + ".json").replaceWithText (juce::JSON::toString (s.toVar()));
}

std::vector<Spectrum> loadReferences()
{
    std::vector<Spectrum> out;
    auto files = referencesFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (const auto& f : files)
    {
        auto s = Spectrum::fromVar (juce::JSON::parse (f));
        if (s.isValid())
            out.push_back (s);
    }
    return out;
}
} // namespace lens::match
