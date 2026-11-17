#pragma once
// Bandlimited wavetable with per-frame mip-maps (SPEC §2).
#include <vector>
#include <string>
#include <memory>

namespace prism::dsp
{
class Wavetable
{
public:
    static constexpr int N = 2048;          // samples per frame
    static constexpr int MaxFrames = 256;
    static constexpr int MaxHarmonics = 512;
    // Mip levels every half octave (twice the SPEC's one-per-octave): level k keeps
    // floor(512 / 2^(k/2)) harmonics, so a note always keeps at least ~71 % of the
    // band up to Nyquist (brighter top end), at ~40 MB per 256-frame table.
    static constexpr int NumLevels = 19;
    static constexpr int Guard = 4;         // stored layout: x[-1], x[0..N-1], x[N], x[N+1], x[N+2]
    static constexpr int Stride = N + Guard;

    // Build from time-domain frames (each N samples, any content). Applies the
    // full table hygiene of SPEC §2 and builds mip levels.
    static std::unique_ptr<Wavetable> fromFrames (const std::vector<std::vector<float>>& frames, const std::string& name);

    int numFrames() const noexcept { return frames; }
    const std::string& getName() const noexcept { return name; }

    // Pointer to sample x[-1] of (frame, level); valid read range [0, N+3].
    const float* levelData (int frame, int level) const noexcept
    {
        return data.data() + ((size_t) frame * NumLevels + (size_t) level) * Stride;
    }
    // Hygienic level-0 frame (N samples), used for display/export/embedding.
    const float* frame0 (int frame) const noexcept { return levelData (frame, 0) + 1; }

    static int harmonicsInLevel (int level) noexcept
    {
        static constexpr int h[NumLevels] = { 512, 362, 256, 181, 128, 90, 64, 45, 32, 22, 16, 11, 8, 5, 4, 2, 2, 1, 1 };
        return h[level];
    }
    // 4-point Hermite while the level is less than 8x oversampled, linear above
    static bool useHermite (int level) noexcept { return harmonicsInLevel (level) > 128; }

    // Debug/test helpers
    std::vector<std::vector<float>> levelZeroFrames() const;

private:
    Wavetable() = default;
    int frames = 0;
    std::string name;
    std::vector<float> data;
};

// Table hygiene on raw frames (in place). Exposed for tests and importer.
// Removes DC, truncates to 512 harmonics, rotates the whole table by one
// offset so frame 0 starts at an upward zero crossing, normalizes to -1 dBFS.
void applyTableHygiene (std::vector<std::vector<float>>& frames);
} // namespace prism::dsp
