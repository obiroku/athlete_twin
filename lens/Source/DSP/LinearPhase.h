#pragma once
// Placeholder until Phase 6.
#include "Types.h"
#include <vector>
namespace lens::dsp
{
class LinearPhaseEngine
{
public:
    void prepare (double, int, int) {}
    void reset() noexcept {}
    void pushParams (const EngineParams&) noexcept {}
    int latency() const noexcept { return 0; }
    void convolve (const double*, const double*, int) noexcept {}
    void combine (double*, double*, int, int, const std::vector<double>*, const EngineParams&) noexcept {}
};
} // namespace lens::dsp
