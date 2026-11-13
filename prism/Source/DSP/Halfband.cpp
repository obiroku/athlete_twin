#include "Halfband.h"
#include <cmath>

namespace prism::dsp
{
namespace
{
constexpr double PI = 3.14159265358979323846;

double accNum (double q, int order, int c)
{
    double acc = 0, term;
    int i = 0, sign = 1;
    do
    {
        term = std::pow (q, (double) i * (i + 1)) * std::sin ((i * 2 + 1) * c * PI / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::abs (term) > 1e-100 && i < 1000);
    return acc;
}

double accDen (double q, int order, int c)
{
    double acc = 0, term;
    int i = 1, sign = -1;
    do
    {
        term = std::pow (q, (double) i * i) * std::cos (i * 2 * c * PI / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::abs (term) > 1e-100 && i < 1000);
    return acc;
}
} // namespace

std::vector<double> designHalfband (int numCoefs, double transition)
{
    double k = std::tan ((1.0 - transition * 2.0) * PI / 4.0);
    k *= k;
    const double kk = std::pow (1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int order = numCoefs * 2 + 1;
    std::vector<double> out ((size_t) numCoefs);
    for (int i = 0; i < numCoefs; ++i)
    {
        const int c = i + 1;
        const double num = accNum (q, order, c) * std::pow (q, 0.25);
        const double den = accDen (q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        out[(size_t) i] = (1.0 - x) / (1.0 + x);
    }
    return out;
}

const HalfbandCoefs<8>& steepCoefs()
{
    static const HalfbandCoefs<8> c (0.04);
    return c;
}

const HalfbandCoefs<4>& relaxedCoefs()
{
    static const HalfbandCoefs<4> c (0.2);
    return c;
}
} // namespace prism::dsp
