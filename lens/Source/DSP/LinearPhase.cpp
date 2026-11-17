#include "LinearPhase.h"

#include "Engine.h"
#include "FilterDesign.h"

namespace lens::dsp
{
namespace
{
enum PoolState : int
{
    Free = 0,
    Building,
    Published,
    InUse
};

juce::uint64 hashParams (const EngineParams& p, int channels) noexcept
{
    juce::uint64 h = 1469598103934665603ULL;
    auto mix = [&h] (const void* data, size_t n)
    {
        const auto* b = static_cast<const unsigned char*> (data);
        for (size_t i = 0; i < n; ++i)
            h = (h ^ b[i]) * 1099511628211ULL;
    };
    for (const auto& b : p.bands)
    {
        const int flags[] = { b.on, (int) b.shape, b.slope, (int) b.stereo, b.isDynamic() };
        mix (flags, sizeof (flags));
        const float v[] = { b.freq, b.gain, b.q };
        mix (v, sizeof (v));
    }
    const int g[] = { (int) p.global.lpResolution, channels };
    mix (g, sizeof (g));
    return h;
}

using M = LinearPhaseEngine::Matrix;

M mul (const M& a, const M& b) noexcept // a·b
{
    return { a.ll * b.ll + a.lr * b.rl, a.ll * b.lr + a.lr * b.rr, a.rl * b.ll + a.rr * b.rl, a.rl * b.lr + a.rr * b.rr };
}

M fromMode (StereoMode m, double h) noexcept
{
    switch (m)
    {
        case StereoMode::Mid: return { 0.5 * (h + 1), 0.5 * (h - 1), 0.5 * (h - 1), 0.5 * (h + 1) };
        case StereoMode::Side: return { 0.5 * (1 + h), 0.5 * (1 - h), 0.5 * (1 - h), 0.5 * (1 + h) };
        case StereoMode::Left: return { h, 0, 0, 1 };
        case StereoMode::Right: return { 1, 0, 0, h };
        case StereoMode::Stereo:
        default: return { h, 0, 0, h };
    }
}

// The part of the signal a dynamic band's change acts on.
M projection (StereoMode m) noexcept
{
    switch (m)
    {
        case StereoMode::Mid: return { 0.5, 0.5, 0.5, 0.5 };
        case StereoMode::Side: return { 0.5, -0.5, -0.5, 0.5 };
        case StereoMode::Left: return { 1, 0, 0, 0 };
        case StereoMode::Right: return { 0, 0, 0, 1 };
        case StereoMode::Stereo:
        default: return { 1, 0, 0, 1 };
    }
}

double magnitude (const BandParams& b, double f) noexcept
{
    if ((b.shape == Shape::LowCut || b.shape == Shape::HighCut) && b.slope == kSlopeBrickwall)
    {
        // Brickwall: flat to the corner, raised-cosine to silence over
        // 1/12 octave.
        const double oct = std::log2 (f / std::max (1.0, (double) b.freq)) * (b.shape == Shape::LowCut ? -1.0 : 1.0);
        if (oct <= 0.0)
            return 1.0;
        if (oct >= 1.0 / 12.0)
            return 0.0;
        return 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * oct * 12.0);
    }
    return std::abs (analogPrototype (b.shape, b.freq, b.gain, b.q, b.slope, f));
}

// Zero-phase S for a dynamic band: band-pass for bells, low/high-pass for
// shelves.
double dynamicShape (const BandParams& b, double f) noexcept
{
    const std::complex<double> s (0.0, f / std::max (1.0, (double) b.freq));
    if (b.shape == Shape::LowShelf)
        return std::abs (1.0 / (s * s + s * 1.4142135623730951 + 1.0));
    if (b.shape == Shape::HighShelf)
        return std::abs (s * s / (s * s + s * 1.4142135623730951 + 1.0));
    const double q = std::max (0.1, (double) b.q);
    return std::abs ((s / q) / (s * s + s / q + 1.0));
}

int fftIndexFor (LpResolution r) noexcept { return (int) r; }
} // namespace

// ---------------------------------------------------------------- kernels

struct LinearPhaseEngine::KernelSet
{
    int taps = 0, B = 0, P = 0;
    struct Path
    {
        bool used = false, cross = false;
        // [P][B+1] spectra per output channel.
        std::array<std::vector<float>, 2> re, im, crossRe, crossIm;
    };
    std::array<Path, numPaths> paths;
    std::array<bool, 2> identity {};
};

int LinearPhaseEngine::partitionSize (int taps) noexcept
{
    return juce::jlimit (256, 8192, taps / 32);
}

std::vector<float> LinearPhaseEngine::kernelFromMagnitude (const std::vector<double>& mag, int taps)
{
    // Frequency sampling on a 2·taps grid → a circular even impulse,
    // centred at taps/2, tapered with a Tukey window over the outer quarter.
    const int fftLen = 2 * taps;
    juce::dsp::FFT f ((int) std::lround (std::log2 (fftLen)));
    std::vector<float> buf ((size_t) (2 * fftLen), 0.0f);
    for (int k = 0; k <= taps; ++k)
        buf[(size_t) (2 * k)] = (float) mag[(size_t) k];
    for (int k = 1; k < taps; ++k)
        buf[(size_t) (2 * (fftLen - k))] = (float) mag[(size_t) k];
    f.performRealOnlyInverseTransform (buf.data());

    std::vector<float> h ((size_t) taps, 0.0f);
    const int c = taps / 2;
    const double taper = taps / 4.0;
    for (int k = 0; k < c; ++k)
    {
        // Average the two halves so the kernel is exactly symmetric.
        const double v = 0.5 * ((double) buf[(size_t) k] + (double) buf[(size_t) ((fftLen - k) % fftLen)]);
        double w = 1.0;
        if (k > c - taper)
            w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (k - (c - taper)) / taper);
        h[(size_t) (c + k)] = (float) (v * w);
        if (k > 0)
            h[(size_t) (c - k)] = (float) (v * w);
    }
    h[0] = 0.0f;
    return h;
}

std::array<int, LinearPhaseEngine::maxDynamicSlots> LinearPhaseEngine::slotBands (const EngineParams& p, int& count)
{
    std::array<int, maxDynamicSlots> s {};
    s.fill (-1);
    count = 0;
    for (int b = 0; b < kNumBands && count < maxDynamicSlots; ++b)
        if (p.bands[(size_t) b].isDynamic())
            s[(size_t) count++] = b;
    return s;
}

void LinearPhaseEngine::buildInto (KernelSet& ks, const EngineParams& p)
{
    const int taps = Engine::linearPhaseTaps (p.global.lpResolution, fs);
    const int b = partitionSize (taps);
    const int parts = taps / b;
    ks.taps = taps;
    ks.B = b;
    ks.P = parts;
    const int bins = taps + 1;
    const double binHz = fs / (2.0 * taps);

    int used = 0;
    const auto slots = slotBands (p, used);

    // Response matrices per bin: static, and each dynamic path.
    std::array<std::vector<M>, numPaths> resp;
    resp[0].assign ((size_t) bins, M {});
    for (int i = 0; i < used; ++i)
        resp[(size_t) i + 1].assign ((size_t) bins, M {});

    std::vector<M> bandM (kNumBands);
    for (int k = 0; k < bins; ++k)
    {
        const double f = std::max (0.5 * binHz, k * binHz);
        M m;
        for (int band = 0; band < kNumBands; ++band)
        {
            const auto& bp = p.bands[(size_t) band];
            auto mode = bp.stereo;
            if (! bp.on || (channels < 2 && mode == StereoMode::Side))
            {
                bandM[(size_t) band] = {};
                continue;
            }
            if (channels < 2)
                mode = StereoMode::Stereo;
            bandM[(size_t) band] = fromMode (mode, magnitude (bp, f));
            m = mul (bandM[(size_t) band], m);
        }
        resp[0][(size_t) k] = m;

        for (int i = 0; i < used; ++i)
        {
            const int d = slots[(size_t) i];
            const auto& bp = p.bands[(size_t) d];
            M before, after;
            for (int band = 0; band < d; ++band)
                before = mul (bandM[(size_t) band], before);
            for (int band = d + 1; band < kNumBands; ++band)
                after = mul (bandM[(size_t) band], after);
            auto pr = projection (channels < 2 ? StereoMode::Stereo : bp.stereo);
            const double s = dynamicShape (bp, f);
            pr = { pr.ll * s, pr.lr * s, pr.rl * s, pr.rr * s };
            resp[(size_t) i + 1][(size_t) k] = mul (after, mul (pr, before));
        }
    }

    // Kernel → partitioned spectra.
    juce::dsp::FFT f (juce::roundToInt (std::log2 (2 * b)));
    std::vector<float> w ((size_t) (8 * b));
    auto partition = [&] (const std::vector<float>& h, std::vector<float>& re, std::vector<float>& im)
    {
        re.assign ((size_t) (parts * (b + 1)), 0.0f);
        im.assign ((size_t) (parts * (b + 1)), 0.0f);
        for (int k = 0; k < parts; ++k)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            std::copy (h.begin() + k * b, h.begin() + (k + 1) * b, w.begin());
            f.performRealOnlyForwardTransform (w.data(), true);
            for (int i = 0; i <= b; ++i)
            {
                re[(size_t) (k * (b + 1) + i)] = w[(size_t) (2 * i)];
                im[(size_t) (k * (b + 1) + i)] = w[(size_t) (2 * i + 1)];
            }
        }
    };

    auto makePath = [&] (const std::vector<M>& r, KernelSet::Path& path)
    {
        path.used = true;
        std::vector<double> ll ((size_t) bins), rr ((size_t) bins), lr ((size_t) bins), rl ((size_t) bins);
        double crossMax = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            ll[(size_t) k] = r[(size_t) k].ll;
            rr[(size_t) k] = r[(size_t) k].rr;
            lr[(size_t) k] = r[(size_t) k].lr;
            rl[(size_t) k] = r[(size_t) k].rl;
            crossMax = std::max ({ crossMax, std::abs (lr[(size_t) k]), std::abs (rl[(size_t) k]) });
        }
        partition (kernelFromMagnitude (ll, taps), path.re[0], path.im[0]);
        if (channels > 1)
            partition (kernelFromMagnitude (rr, taps), path.re[1], path.im[1]);
        path.cross = channels > 1 && crossMax > 1.0e-9;
        if (path.cross)
        {
            // Output L gets LR·R, output R gets RL·L.
            partition (kernelFromMagnitude (lr, taps), path.crossRe[0], path.crossIm[0]);
            partition (kernelFromMagnitude (rl, taps), path.crossRe[1], path.crossIm[1]);
        }
    };

    makePath (resp[0], ks.paths[0]);
    for (int i = 1; i < numPaths; ++i)
    {
        if (i - 1 < used)
            makePath (resp[(size_t) i], ks.paths[(size_t) i]);
        else
            ks.paths[(size_t) i].used = false;
    }

    ks.identity = { true, channels > 1 };
    for (int k = 0; k < bins; ++k)
    {
        const auto& m = resp[0][(size_t) k];
        if (std::abs (m.ll - 1.0) > 1.0e-12 || std::abs (m.lr) > 1.0e-12)
            ks.identity[0] = false;
        if (std::abs (m.rr - 1.0) > 1.0e-12 || std::abs (m.rl) > 1.0e-12)
            ks.identity[1] = false;
    }
}

// ---------------------------------------------------------------- builder

class LinearPhaseEngine::Builder : public juce::Thread
{
public:
    explicit Builder (LinearPhaseEngine& o) : juce::Thread ("LENS linear phase"), owner (o) {}

    void run() override
    {
        EngineParams latest {};
        bool dirty = false;
        double firstChange = 0.0, lastChange = 0.0;
        while (! threadShouldExit())
        {
            EngineParams p;
            bool got = false;
            while (owner.paramQueue.pop (p))
            {
                latest = p;
                got = true;
            }
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (got)
            {
                if (! dirty)
                    firstChange = now;
                lastChange = now;
                dirty = true;
            }
            // Debounce 30 ms, but rebuild at least every 150 ms while dragging.
            if (dirty && (now - lastChange >= 30.0 || now - firstChange >= 150.0))
                dirty = ! publish (latest);
            wait (dirty ? 5 : 20);
        }
    }

    bool publish (const EngineParams& p)
    {
        int slot = -1;
        for (int i = 0; i < poolSize && slot < 0; ++i)
        {
            int expected = Free;
            if (owner.poolState[(size_t) i].compare_exchange_strong (expected, Building))
                slot = i;
        }
        if (slot < 0)
            return false; // all busy: try again shortly
        owner.buildInto (*owner.pool[(size_t) slot], p);
        owner.poolState[(size_t) slot].store (Published, std::memory_order_release);
        const int old = owner.pending.exchange (slot, std::memory_order_acq_rel);
        if (old >= 0)
            owner.poolState[(size_t) old].store (Free); // never picked up
        return true;
    }

private:
    LinearPhaseEngine& owner;
};

// ---------------------------------------------------------------- engine

LinearPhaseEngine::LinearPhaseEngine()
{
    for (auto& p : pool)
        p = std::make_unique<KernelSet>();
    for (auto& s : poolState)
        s.store (Free);
}

LinearPhaseEngine::~LinearPhaseEngine()
{
    if (builder != nullptr)
        builder->stopThread (4000);
}

void LinearPhaseEngine::prepare (double sampleRate, int block, int numChannels)
{
    if (builder != nullptr)
        builder->stopThread (4000);

    fs = sampleRate;
    maxBlock = std::max (1, block);
    channels = numChannels > 1 ? 2 : 1;

    maxB = 0;
    maxP = 0;
    for (auto r : { LpResolution::Low, LpResolution::Medium, LpResolution::High })
    {
        const int t = Engine::linearPhaseTaps (r, fs);
        const int b = partitionSize (t);
        maxB = std::max (maxB, b);
        maxP = std::max (maxP, t / b);
        ffts[(size_t) fftIndexFor (r)] = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2 * b)));
    }
    for (int c = 0; c < 2; ++c)
    {
        inBlock[(size_t) c].assign ((size_t) maxB, 0.0f);
        prevBlock[(size_t) c].assign ((size_t) maxB, 0.0f);
        fdlRe[(size_t) c].assign ((size_t) (maxP * (maxB + 1)), 0.0f);
        fdlIm[(size_t) c].assign ((size_t) (maxP * (maxB + 1)), 0.0f);
    }
    work.assign ((size_t) (8 * maxB), 0.0f);
    accRe.assign ((size_t) (maxB + 1), 0.0f);
    accIm.assign ((size_t) (maxB + 1), 0.0f);
    for (auto& path : outCur)
        for (auto& v : path)
            v.assign ((size_t) maxB, 0.0f);
    for (auto& path : outOld)
        for (auto& v : path)
            v.assign ((size_t) maxB, 0.0f);

    staticOut.setSize (2, maxBlock);
    for (auto& b : slotOut)
        b.setSize (2, maxBlock);

    delayLen = latencyFor (Engine::linearPhaseTaps (LpResolution::High, fs)) + 1;
    for (auto& d : delayLine)
        d.assign ((size_t) delayLen, 0.0f);
    for (auto& g : gainDelay)
        g.assign ((size_t) delayLen, 0.0f);
    identityStep = 1.0 / (0.010 * fs);

    pending.store (-1);
    for (auto& s : poolState)
        s.store (Free);
    active = fading = -1;
    lastHash = 0;
    EngineParams dummy;
    while (paramQueue.pop (dummy))
    {
    }
    setResolution (LpResolution::Medium);

    builder = std::make_unique<Builder> (*this);
    builder->startThread (juce::Thread::Priority::low);
}

void LinearPhaseEngine::setResolution (LpResolution r) noexcept
{
    currentTaps = Engine::linearPhaseTaps (r, fs);
    B = partitionSize (currentTaps);
    P = currentTaps / B;
    fft = ffts[(size_t) fftIndexFor (r)].get();
}

void LinearPhaseEngine::prepareKernels (const EngineParams& p)
{
    setResolution (p.global.lpResolution);
    if (p.global.linearPhase)
    {
        // Not the audio thread: build and activate synchronously.
        poolState[0].store (InUse);
        buildInto (*pool[0], p);
        active = 0;
        lastHash = hashParams (p, channels);
    }
    reset();
}

void LinearPhaseEngine::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inBlock[(size_t) c].begin(), inBlock[(size_t) c].end(), 0.0f);
        std::fill (prevBlock[(size_t) c].begin(), prevBlock[(size_t) c].end(), 0.0f);
        std::fill (fdlRe[(size_t) c].begin(), fdlRe[(size_t) c].end(), 0.0f);
        std::fill (fdlIm[(size_t) c].begin(), fdlIm[(size_t) c].end(), 0.0f);
        std::fill (delayLine[(size_t) c].begin(), delayLine[(size_t) c].end(), 0.0f);
    }
    for (auto& path : outCur)
        for (auto& v : path)
            std::fill (v.begin(), v.end(), 0.0f);
    for (auto& g : gainDelay)
        std::fill (g.begin(), g.end(), 0.0f);
    head = fill = 0;
    delayPos = 0;
    gainPos.fill (0);
    if (fading >= 0)
    {
        poolState[(size_t) fading].store (Free);
        fading = -1;
    }
    fadeBlocksLeft = 0;
    fadePos = 1.0;
    const bool ok = active >= 0 && pool[(size_t) active]->taps == currentTaps;
    identityMix = { ok && pool[(size_t) active]->identity[0] ? 1.0 : 0.0, ok && pool[(size_t) active]->identity[1] ? 1.0 : 0.0 };
}

bool LinearPhaseEngine::ready() const noexcept
{
    return active >= 0 && pool[(size_t) active]->taps == currentTaps;
}

void LinearPhaseEngine::pushParams (const EngineParams& p, bool offline) noexcept
{
    const auto h = hashParams (p, channels);
    if (h == lastHash)
        return;
    if (offline)
    {
        // Offline rendering is not real-time: building here (it allocates)
        // keeps bounces identical however fast the host renders.
        for (int i = 0; i < poolSize; ++i)
        {
            int expected = Free;
            if (poolState[(size_t) i].compare_exchange_strong (expected, Building))
            {
                buildInto (*pool[(size_t) i], p);
                poolState[(size_t) i].store (Published, std::memory_order_release);
                const int old = pending.exchange (i, std::memory_order_acq_rel);
                if (old >= 0)
                    poolState[(size_t) old].store (Free);
                lastHash = h;
                return;
            }
        }
        return; // pool busy: try again next block
    }
    if (paramQueue.push (p))
        lastHash = h; // if the queue is full, try again next block
}

void LinearPhaseEngine::convolvePath (const KernelSet& ks, int path, int c, float* out) noexcept
{
    const auto& pth = ks.paths[(size_t) path];
    const int b1 = B + 1;
    float* ar = accRe.data();
    float* ai = accIm.data();
    std::fill (ar, ar + b1, 0.0f);
    std::fill (ai, ai + b1, 0.0f);

    auto mac = [&] (const std::vector<float>& xRe, const std::vector<float>& xIm, const std::vector<float>& hRe, const std::vector<float>& hIm)
    {
        for (int k = 0; k < P; ++k)
        {
            int idx = head - k;
            if (idx < 0)
                idx += P;
            const float* xr = xRe.data() + idx * b1;
            const float* xi = xIm.data() + idx * b1;
            const float* hr = hRe.data() + k * b1;
            const float* hi = hIm.data() + k * b1;
            for (int i = 0; i < b1; ++i)
            {
                ar[i] += xr[i] * hr[i] - xi[i] * hi[i];
                ai[i] += xr[i] * hi[i] + xi[i] * hr[i];
            }
        }
    };
    mac (fdlRe[(size_t) c], fdlIm[(size_t) c], pth.re[(size_t) c], pth.im[(size_t) c]);
    if (pth.cross)
        mac (fdlRe[(size_t) (1 - c)], fdlIm[(size_t) (1 - c)], pth.crossRe[(size_t) c], pth.crossIm[(size_t) c]);

    const int n = 2 * B;
    float* w = work.data();
    for (int i = 0; i <= B; ++i)
    {
        w[2 * i] = ar[i];
        w[2 * i + 1] = ai[i];
    }
    for (int i = 1; i < B; ++i)
    {
        w[2 * (n - i)] = ar[i];
        w[2 * (n - i) + 1] = -ai[i];
    }
    fft->performRealOnlyInverseTransform (w);
    std::copy (w + B, w + 2 * B, out);
}

void LinearPhaseEngine::computeBlock() noexcept
{
    // New kernels? Take them at a block boundary and keep the old ones for
    // a short crossfade: the shared input history makes this seamless.
    const int idx = pending.exchange (-1, std::memory_order_acq_rel);
    if (idx >= 0)
    {
        if (pool[(size_t) idx]->taps != currentTaps)
            poolState[(size_t) idx].store (Free); // built for another resolution
        else
        {
            if (fading >= 0)
                poolState[(size_t) fading].store (Free);
            poolState[(size_t) idx].store (InUse);
            fading = (active >= 0 && pool[(size_t) active]->taps == currentTaps) ? active : -1;
            if (active >= 0 && fading < 0)
                poolState[(size_t) active].store (Free);
            active = idx;
            if (fading >= 0)
            {
                fadeBlocks = std::max (1, juce::roundToInt (0.02 * fs / B));
                fadeBlocksLeft = fadeBlocks;
                fadeStep = 1.0 / (fadeBlocks * B);
                fadePos = 0.0;
            }
        }
    }
    else if (fading >= 0 && fadeBlocksLeft <= 0)
    {
        poolState[(size_t) fading].store (Free);
        fading = -1;
    }

    // Forward FFT of the newest 2B samples, per channel, into the history.
    const int b1 = B + 1;
    float* w = work.data();
    for (int c = 0; c < channels; ++c)
    {
        std::fill (w, w + 4 * B, 0.0f);
        std::copy (prevBlock[(size_t) c].begin(), prevBlock[(size_t) c].begin() + B, w);
        std::copy (inBlock[(size_t) c].begin(), inBlock[(size_t) c].begin() + B, w + B);
        fft->performRealOnlyForwardTransform (w, true);
        float* re = fdlRe[(size_t) c].data() + head * b1;
        float* im = fdlIm[(size_t) c].data() + head * b1;
        for (int i = 0; i <= B; ++i)
        {
            re[i] = w[2 * i];
            im[i] = w[2 * i + 1];
        }
        std::copy (inBlock[(size_t) c].begin(), inBlock[(size_t) c].begin() + B, prevBlock[(size_t) c].begin());
    }

    auto runSet = [&] (int setIndex, std::array<std::array<std::vector<float>, 2>, numPaths>& dst)
    {
        const auto& ks = *pool[(size_t) setIndex];
        for (int path = 0; path < numPaths; ++path)
            if (ks.paths[(size_t) path].used)
                for (int c = 0; c < channels; ++c)
                    convolvePath (ks, path, c, dst[(size_t) path][(size_t) c].data());
    };
    if (active >= 0 && pool[(size_t) active]->taps == currentTaps)
        runSet (active, outCur);
    if (fading >= 0)
    {
        runSet (fading, outOld);
        --fadeBlocksLeft;
    }

    head = head + 1 == P ? 0 : head + 1;
}

void LinearPhaseEngine::convolve (const double* L, const double* R, int n) noexcept
{
    const bool okSet = active >= 0 && pool[(size_t) active]->taps == currentTaps;
    const KernelSet* cur = okSet ? pool[(size_t) active].get() : nullptr;
    const int d = std::min (latencyFor (currentTaps), delayLen - 1);

    for (int i = 0; i < n; ++i)
    {
        const KernelSet* old = fading >= 0 ? pool[(size_t) fading].get() : nullptr;
        cur = active >= 0 && pool[(size_t) active]->taps == currentTaps ? pool[(size_t) active].get() : nullptr;
        const double r = old != nullptr ? std::min (1.0, fadePos) : 1.0;
        if (old != nullptr)
            fadePos += fadeStep;

        const float x[2] = { (float) L[i], (float) R[i] };
        for (int c = 0; c < channels; ++c)
        {
            inBlock[(size_t) c][(size_t) fill] = x[c];

            // Static path, or the exact delay for an untouched channel.
            float y = 0.0f;
            if (cur != nullptr)
            {
                y = outCur[0][(size_t) c][(size_t) fill];
                if (old != nullptr)
                    y = (float) (outOld[0][(size_t) c][(size_t) fill] * (1.0 - r) + y * r);
            }
            delayLine[(size_t) c][(size_t) delayPos] = x[c];
            int rd = delayPos - d;
            if (rd < 0)
                rd += delayLen;
            const float delayed = delayLine[(size_t) c][(size_t) rd];
            auto& mix = identityMix[(size_t) c];
            const bool id = cur != nullptr && cur->identity[(size_t) c];
            mix = id ? std::min (1.0, mix + identityStep) : std::max (0.0, mix - identityStep);
            if (mix >= 1.0)
                y = delayed;
            else if (mix > 0.0)
                y = (float) (y + (delayed - y) * mix);
            staticOut.setSample (c, i, y);

            for (int s = 0; s < maxDynamicSlots; ++s)
            {
                float v = 0.0f;
                if (cur != nullptr && cur->paths[(size_t) s + 1].used)
                    v = outCur[(size_t) s + 1][(size_t) c][(size_t) fill];
                if (old != nullptr)
                {
                    const float o = old->paths[(size_t) s + 1].used ? outOld[(size_t) s + 1][(size_t) c][(size_t) fill] : 0.0f;
                    v = (float) (o * (1.0 - r) + v * r);
                }
                slotOut[(size_t) s].setSample (c, i, v);
            }
        }
        delayPos = delayPos + 1 == delayLen ? 0 : delayPos + 1;

        if (++fill == B)
        {
            fill = 0;
            computeBlock();
        }
    }
}

void LinearPhaseEngine::combine (double* wl, double* wr, int off, int n, const std::vector<double>* dynGain, const EngineParams& p) noexcept
{
    const bool stereo = channels > 1;
    for (int i = 0; i < n; ++i)
    {
        wl[i] = staticOut.getSample (0, off + i);
        wr[i] = stereo ? staticOut.getSample (1, off + i) : wl[i];
    }

    int used = 0;
    const auto slots = slotBands (p, used);
    const int delay = std::clamp (latencyFor (currentTaps) - lookahead, 0, delayLen - 1);
    for (int s = 0; s < used; ++s)
    {
        const int b = slots[(size_t) s];
        const double gs = std::pow (10.0, p.bands[(size_t) b].gain / 20.0);
        auto& line = gainDelay[(size_t) s];
        int pos = gainPos[(size_t) s];
        const auto& g = dynGain[(size_t) b];
        for (int i = 0; i < n; ++i)
        {
            line[(size_t) pos] = (float) g[(size_t) (off + i)];
            int rd = pos - delay;
            if (rd < 0)
                rd += delayLen;
            const double gd = line[(size_t) rd];
            pos = pos + 1 == delayLen ? 0 : pos + 1;
            if (gd == 0.0) // NOLINT exact: no dynamic change
                continue;
            const double c = gs * (std::pow (10.0, gd / 20.0) - 1.0);
            wl[i] += c * slotOut[(size_t) s].getSample (0, off + i);
            wr[i] = stereo ? wr[i] + c * slotOut[(size_t) s].getSample (1, off + i) : wl[i];
        }
        gainPos[(size_t) s] = pos;
    }
}
} // namespace lens::dsp
