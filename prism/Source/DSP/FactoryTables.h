#pragma once
#include <vector>
#include <string>

namespace prism::dsp
{
// The 24 factory tables of SPEC §3. Generated procedurally and
// deterministically (fixed seeds). Returns raw frames (2048 samples each);
// the caller applies hygiene via Wavetable::fromFrames.
const std::vector<std::string>& factoryTableNames();
int factoryTableIndex (const std::string& name); // -1 if unknown
std::vector<std::vector<float>> generateFactoryFrames (int index, int numFrames = 256);
} // namespace prism::dsp
