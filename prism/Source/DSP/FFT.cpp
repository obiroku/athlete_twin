#include "FFT.h"
#include <cmath>
#include <algorithm>

namespace prism::dsp
{
void fft (std::vector<cplx>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    // twiddle table for this size (cached per thread)
    thread_local std::vector<cplx> table;
    thread_local size_t tableN = 0;
    if (tableN != n)
    {
        table.resize (n / 2 + 1);
        for (size_t k = 0; k <= n / 2; ++k)
        {
            const double ang = -2.0 * 3.14159265358979323846 * (double) k / (double) n;
            table[k] = cplx (std::cos (ang), std::sin (ang));
        }
        tableN = n;
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t half = len / 2;
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < half; ++k)
            {
                cplx w = table[k * step];
                if (inverse) w = std::conj (w);
                const cplx u = a[i + k];
                const cplx v = a[i + k + half] * w;
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
    }
}

template <typename T>
static std::vector<cplx> toHarm (const T* x, int n)
{
    std::vector<cplx> a ((size_t) n);
    for (int i = 0; i < n; ++i) a[(size_t) i] = cplx ((double) x[i], 0.0);
    fft (a, false);
    std::vector<cplx> h ((size_t) n / 2 + 1);
    h[0] = a[0] / (double) n;
    for (int k = 1; k <= n / 2; ++k) h[(size_t) k] = a[(size_t) k] * (2.0 / (double) n);
    return h;
}

std::vector<cplx> realToHarmonics (const float* x, int n) { return toHarm (x, n); }
std::vector<cplx> realToHarmonics (const double* x, int n) { return toHarm (x, n); }

template <typename T>
static void fromHarm (const std::vector<cplx>& harm, int maxHarm, T* out, int n)
{
    std::vector<cplx> a ((size_t) n, cplx (0, 0));
    const int H = std::min ({ maxHarm, n / 2 - 1, (int) harm.size() - 1 });
    if (! harm.empty()) a[0] = harm[0] * (double) n;
    for (int k = 1; k <= H; ++k)
    {
        a[(size_t) k] = harm[(size_t) k] * ((double) n * 0.5);
        a[(size_t) (n - k)] = std::conj (a[(size_t) k]);
    }
    fft (a, true);
    for (int i = 0; i < n; ++i) out[i] = (T) (a[(size_t) i].real() / (double) n);
}

void harmonicsToReal (const std::vector<cplx>& harm, int maxHarm, float* out, int n) { fromHarm (harm, maxHarm, out, n); }
void harmonicsToReal (const std::vector<cplx>& harm, int maxHarm, double* out, int n) { fromHarm (harm, maxHarm, out, n); }

std::vector<double> blackmanHarris (int n)
{
    std::vector<double> w ((size_t) n);
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * 3.14159265358979323846 * i / (double) (n - 1);
        w[(size_t) i] = a0 - a1 * std::cos (t) + a2 * std::cos (2 * t) - a3 * std::cos (3 * t);
    }
    return w;
}
} // namespace prism::dsp
