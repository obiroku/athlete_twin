#pragma once
// Placeholder until Phase 6.
#include "Types.h"
namespace lens::dsp
{
class LinearPhaseEngine
{
public:
    void prepare (double, int, int) {}
    void reset() noexcept {}
    void pushParams (const EngineParams&) noexcept {}
    int latency() const noexcept { return 0; }
    void process (const double*, const double*, double*, double*, int, const std::vector<double>*, const EngineParams&) noexcept {}
};
} // namespace lens::dsp
