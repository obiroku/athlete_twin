#pragma once
#include "FxBase.h"
#include <memory>

namespace prism::dsp
{
// Create the module for an FxId (see Engine/ModTypes.h for the order).
std::unique_ptr<FxModule> createFxModule (int fxId);
} // namespace prism::dsp
