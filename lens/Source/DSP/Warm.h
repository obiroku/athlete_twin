#pragma once
// Placeholder until Phase 7.
namespace lens::dsp
{
class WarmStage
{
public:
    void prepare (double, int, int) {}
    void reset() noexcept {}
    int latency() const noexcept { return 0; }
    void process (double*, double*, int) noexcept {}
};
} // namespace lens::dsp
