#include "Filters.h"
#include <complex>

namespace prism::dsp
{
// ------------------------------------------------------------- diode ladder model
// Linear diode-ladder state space (normalised w = 1):
//  y1' = u - 2 y1 + y2
//  y2' = (y1 - 2 y2 + y3) / 2
//  y3' = (y2 - 2 y3 + y4) / 2
//  y4' = (y3 - y4) / 2
static std::complex<double> diodeH (double w)
{
    using C = std::complex<double>;
    const C s (0.0, w);
    // Solve (sI - A) y = b u with b = [1,0,0,0], by Gaussian elimination.
    C M[4][5] = {
        { s + 2.0, -1.0, 0.0, 0.0, 1.0 },
        { -0.5, s + 1.0, -0.5, 0.0, 0.0 },
        { 0.0, -0.5, s + 1.0, -0.5, 0.0 },
        { 0.0, 0.0, -0.5, s + 0.5, 0.0 } };
    for (int i = 0; i < 4; ++i)
        for (int r = i + 1; r < 4; ++r)
        {
            const C f = M[r][i] / M[i][i];
            for (int c = i; c < 5; ++c) M[r][c] -= f * M[i][c];
        }
    C y[4];
    for (int i = 3; i >= 0; --i)
    {
        C acc = M[i][4];
        for (int c = i + 1; c < 4; ++c) acc -= M[i][c] * y[c];
        y[i] = acc / M[i][i];
    }
    return y[3];
}

const DiodeConstants& diodeConstants()
{
    static const DiodeConstants dc = [] {
        DiodeConstants d;
        // find w where phase crosses -180 degrees
        double lo = 0.01, hi = 10.0;
        for (int it = 0; it < 200; ++it)
        {
            const double mid = std::sqrt (lo * hi);
            const auto h = diodeH (mid);
            // phase unwrapped: imag sign change at -180 means real < 0 and imag crosses 0
            if (h.imag() < 0.0) lo = mid; else hi = mid;
        }
        const double w = std::sqrt (lo * hi);
        d.oscRatio = w;
        d.kCrit = 1.0 / std::abs (diodeH (w));
        return d;
    }();
    return dc;
}

VowelFormants vowelAt (float v) noexcept
{
    static const float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    static const float Gn[5][3] = { { 1.0f, 0.5f, 0.25f }, { 1.0f, 0.45f, 0.3f }, { 1.0f, 0.35f, 0.3f }, { 1.0f, 0.5f, 0.2f }, { 1.0f, 0.4f, 0.15f } };
    v = clampf (v, 0.0f, 1.0f) * 4.0f;
    int i = std::min (3, (int) v);
    const float t = v - (float) i;
    VowelFormants r;
    for (int j = 0; j < 3; ++j)
    {
        r.f[j] = F[i][j] * std::pow (F[i + 1][j] / F[i][j], t);
        r.g[j] = Gn[i][j] + (Gn[i + 1][j] - Gn[i][j]) * t;
    }
    return r;
}

// ------------------------------------------------------------- core

void FilterCore::prepare (double sampleRate)
{
    sr = sampleRate;
    combLen = (int) std::ceil (sr / 20.0) + 8;
    for (auto& c : comb) c.assign ((size_t) combLen, 0.0f);
    (void) diodeConstants();
    reset();
}

void FilterCore::reset() noexcept
{
    s1 = {}; s2 = {};
    for (auto& f : fs) f = {};
    for (auto& a : lad) a[0] = a[1] = 0.0f;
    for (auto& a : dio) for (auto& v : a) v = 0.0;
    for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
    combW = 0;
    lastCut = -1.0f;
}

void FilterCore::updateCoefs (float fc, float reso, float vowel) noexcept
{
    lastCut = fc; lastReso = reso; lastVowel = vowel;
    const float nyq = (float) sr * 0.45f;
    fc = clampf (fc, 5.0f, nyq);
    switch (type)
    {
        case FilterType::Ladder:
        {
            g = fastTan (kPiF * fc / (float) sr);
            G = g / (1.0f + g);
            ladK = reso >= 1.0f ? 4.15f : 4.15f * reso;
            break;
        }
        case FilterType::Diode:
        {
            const auto& dc = diodeConstants();
            const float fint = clampf (fc / (float) dc.oscRatio, 5.0f, nyq);
            dioG = std::tan (3.14159265358979 * (double) fint / sr);
            dioK = dc.kCrit * 1.03 * (double) reso;
            break;
        }
        case FilterType::CombPlus:
        case FilterType::CombMinus:
        {
            combDelay = clampf ((float) sr / std::max (20.0f, fc), 2.0f, (float) combLen - 4.0f);
            combFb = reso * 0.995f;
            break;
        }
        case FilterType::Formant:
        {
            const auto vf = vowelAt (vowel);
            const float Q = 6.0f + 24.0f * reso;
            for (int i = 0; i < 3; ++i)
            {
                const float gg = fastTan (kPiF * clampf (vf.f[i], 20.0f, nyq) / (float) sr);
                fk[i] = 1.0f / Q;
                fa1[i] = 1.0f / (1.0f + gg * (gg + fk[i]));
                fa2[i] = gg * fa1[i];
                fa3[i] = gg * fa2[i];
                fgain[i] = vf.g[i];
            }
            break;
        }
        default:
        {
            g = fastTan (kPiF * fc / (float) sr);
            // reso 0..1 -> damping 2 .. slightly negative (self-oscillation, bounded below)
            const float r = clampf (reso, 0.0f, 1.0f);
            k = 2.0f * (1.0f - r);
            satStates = r > 0.97f;
            if (satStates) k -= 0.03f * (r - 0.97f) / 0.03f;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            b1 = 1.0f / (1.0f + g * (g + k2));
            b2 = g * b1;
            b3 = g * b2;
            break;
        }
    }
}

inline float FilterCore::svfTick (Svf& s, int ch, float v0, float A1, float A2, float A3, float& bp, float& hp, float K) noexcept
{
    const float v3 = v0 - s.ic2[ch];
    const float v1 = A1 * s.ic1[ch] + A2 * v3;
    const float v2 = s.ic2[ch] + A2 * s.ic1[ch] + A3 * v3;
    s.ic1[ch] = 2.0f * v1 - s.ic1[ch];
    s.ic2[ch] = 2.0f * v2 - s.ic2[ch];
    bp = v1;
    hp = v0 - K * v1 - v2;
    return v2;
}

void FilterCore::process (float& l, float& r, float fc, float reso, float vowel) noexcept
{
    if (fc != lastCut || reso != lastReso || (type == FilterType::Formant && vowel != lastVowel))
        updateCoefs (fc, reso, vowel);

    float io[2] = { l, r };
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = io[ch];
        float y = x;
        switch (type)
        {
            case FilterType::LP12: case FilterType::HP12: case FilterType::BP12:
            case FilterType::Notch: case FilterType::Peak:
            {
                float bp, hp;
                const float lp = svfTick (s1, ch, x, a1, a2, a3, bp, hp, k);
                if (satStates) { s1.ic1[ch] = 3.0f * tanhFast (s1.ic1[ch] * (1.0f / 3.0f)); s1.ic2[ch] = 3.0f * tanhFast (s1.ic2[ch] * (1.0f / 3.0f)); }
                switch (type)
                {
                    case FilterType::LP12: y = lp; break;
                    case FilterType::HP12: y = hp; break;
                    case FilterType::BP12: y = bp * std::max (k, 0.3f); break;
                    case FilterType::Notch: y = x - k * bp; break;
                    default: y = x + 2.0f * std::max (k, 0.05f) * bp; break;
                }
                break;
            }
            case FilterType::LP24: case FilterType::HP24: case FilterType::BP24:
            {
                float bp, hp, bp2, hp2;
                const float lp = svfTick (s1, ch, x, a1, a2, a3, bp, hp, k);
                if (satStates) { s1.ic1[ch] = 3.0f * tanhFast (s1.ic1[ch] * (1.0f / 3.0f)); s1.ic2[ch] = 3.0f * tanhFast (s1.ic2[ch] * (1.0f / 3.0f)); }
                const float in2 = type == FilterType::LP24 ? lp : (type == FilterType::HP24 ? hp : bp * std::max (k, 0.3f));
                const float lp2 = svfTick (s2, ch, in2, b1, b2, b3, bp2, hp2, k2);
                y = type == FilterType::LP24 ? lp2 : (type == FilterType::HP24 ? hp2 : bp2 * k2);
                break;
            }
            case FilterType::Ladder:
            {
                float* s = lad[0] + 0; (void) s;
                const float S = G * G * G * lad[0][ch] + G * G * lad[1][ch] + G * lad[2][ch] + lad[3][ch];
                const float G4 = G * G * G * G;
                const float invg = 1.0f / (1.0f + g);
                const float Sn = S * invg;
                const float y4lin = (G4 * x + Sn) / (1.0f + ladK * G4);
                const float u = tanhFast (x - ladK * y4lin);
                float in = u;
                for (int i = 0; i < 4; ++i)
                {
                    const float v = (in - lad[i][ch]) * G;
                    const float out = v + lad[i][ch];
                    lad[i][ch] = out + v;
                    in = out;
                }
                y = in * (1.0f + 0.5f * ladK);
                break;
            }
            case FilterType::Diode:
            {
                // trapezoidal state-space solve: (I - gM) y = g b x + s
                const double gg = dioG, kk = dioK;
                double* st = dio[ch];
                const double xin = tanhFast (x);
                // M rows (with feedback u = x - k y4 folded in)
                double A[4][5] = {
                    { 1.0 + 2.0 * gg, -gg, 0.0, gg * kk, gg * xin + st[0] },
                    { -0.5 * gg, 1.0 + gg, -0.5 * gg, 0.0, st[1] },
                    { 0.0, -0.5 * gg, 1.0 + gg, -0.5 * gg, st[2] },
                    { 0.0, 0.0, -0.5 * gg, 1.0 + 0.5 * gg, st[3] } };
                for (int i = 0; i < 4; ++i)
                    for (int rr = i + 1; rr < 4; ++rr)
                    {
                        if (A[rr][i] == 0.0) continue;
                        const double f = A[rr][i] / A[i][i];
                        for (int c = i; c < 5; ++c) A[rr][c] -= f * A[i][c];
                    }
                double yv[4];
                for (int i = 3; i >= 0; --i)
                {
                    double acc = A[i][4];
                    for (int c = i + 1; c < 4; ++c) acc -= A[i][c] * yv[c];
                    yv[i] = acc / A[i][i];
                }
                for (int i = 0; i < 4; ++i)
                {
                    double ns = 2.0 * yv[i] - st[i];
                    if (ns > 2.5 || ns < -2.5) ns = 2.5 * std::tanh (ns / 2.5);
                    st[i] = ns;
                }
                y = (float) yv[3] * (1.0f + (float) (kk * 0.08));
                break;
            }
            case FilterType::CombPlus:
            case FilterType::CombMinus:
            {
                auto& buf = comb[ch];
                float rp = (float) combW - combDelay;
                while (rp < 0.0f) rp += (float) combLen;
                int i0 = (int) rp;
                const float f = rp - (float) i0;
                int i1 = i0 + 1; if (i1 >= combLen) i1 = 0;
                const float d = buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
                const float fb = type == FilterType::CombPlus ? combFb : -combFb;
                float w = x + fb * d;
                w = 2.0f * tanhFast (w * 0.5f);
                buf[(size_t) combW] = w;
                y = w * (1.0f - 0.6f * combFb);
                break;
            }
            case FilterType::Formant:
            {
                float sum = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    float bp, hp;
                    svfTick (fs[i], ch, x, fa1[i], fa2[i], fa3[i], bp, hp, fk[i]);
                    sum += fgain[i] * bp * fk[i];
                }
                y = sum * 1.6f;
                break;
            }
            default: break;
        }
        io[ch] = y;
    }
    if (type == FilterType::CombPlus || type == FilterType::CombMinus)
        if (++combW >= combLen) combW = 0;
    l = io[0];
    r = io[1];
}

// ------------------------------------------------------------- wrapper with type crossfade + drive

void Filter::prepare (double sampleRate)
{
    core[0].prepare (sampleRate);
    core[1].prepare (sampleRate);
    xfadeLen = std::max (1, (int) std::lround (0.005 * sampleRate));
    (void) steepCoefs();
    reset();
}

void Filter::reset() noexcept
{
    core[0].reset(); core[1].reset();
    core[0].setType (type); core[1].setType (type);
    xfadeRemain = 0;
    drv[0].reset(); drv[1].reset();
}

void Filter::setType (FilterType t) noexcept
{
    if (t == type) return;
    type = t;
    const int other = 1 - active;
    core[other].reset();
    core[other].setType (t);
    active = other;
    xfadeRemain = xfadeLen;
}

void Filter::process (float& l, float& r, float cutoffHz, float reso, float vowel, float driveDb, float mix) noexcept
{
    const float dryL = l, dryR = r;
    float inL = l, inR = r;
    if (driveDb > 0.01f)
    {
        if (lastDrive <= 0.01f) { drv[0].reset(); drv[1].reset(); }
        const float gd = dbToGain (driveDb);
        const float comp = 1.0f / std::sqrt (gd);
        auto sat = [gd, comp] (float x) { return tanhFast (x * gd) * comp; };
        inL = drv[0].process (inL, sat);
        inR = drv[1].process (inR, sat);
    }
    lastDrive = driveDb;

    float aL = inL, aR = inR;
    core[active].process (aL, aR, cutoffHz, reso, vowel);
    if (xfadeRemain > 0)
    {
        float bL = inL, bR = inR;
        core[1 - active].process (bL, bR, cutoffHz, reso, vowel);
        const float t = 1.0f - (float) xfadeRemain / (float) xfadeLen;
        aL = bL + (aL - bL) * t;
        aR = bR + (aR - bR) * t;
        --xfadeRemain;
    }
    if (mix >= 1.0f) { l = aL; r = aR; }
    else { l = dryL + (aL - dryL) * mix; r = dryR + (aR - dryR) * mix; }
}
} // namespace prism::dsp
