#pragma once
// Placeholder until Phase 7.
#include "Types.h"
namespace lens::dsp
{
class SpectralProcessor
{
public:
    void prepare (double, int, int) {}
    void reset() noexcept {}
    int latency() const noexcept { return 0; }
    void process (double*, double*, int, const EngineParams&) noexcept {}
};
} // namespace lens::dsp
