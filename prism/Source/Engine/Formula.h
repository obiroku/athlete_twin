#pragma once
// Small in-house expression language for wavetable formulas (SPEC §3).
// Variables: x (phase 0..1), y (frame position 0..1), pi, e.
// Functions: sin cos tan abs sqrt pow exp log min max floor sign tri saw square rand.
// Evaluation never produces NaN/inf: any non-finite result becomes 0.
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace prism
{
class Formula
{
public:
    // Returns nullptr and fills `error` on a parse error.
    static std::unique_ptr<Formula> parse (const std::string& text, std::string& error);
    double eval (double x, double y) const noexcept;
    // Builds `frames` frames of 2048 samples (raw; hygiene is applied later).
    std::vector<std::vector<float>> render (int frames, uint64_t seed = 0x5EED) const;

    struct Node
    {
        enum Op : uint8_t { Num, VarX, VarY, Add, Sub, Mul, Div, Mod, Pow, Neg, Func } op = Num;
        uint8_t func = 0, nargs = 0;
        double value = 0;
        int a = -1, b = -1;   // children
    };
private:
    std::vector<Node> nodes;
    int root = -1;
    mutable uint64_t randState = 0x5EED;
    double evalNode (int n, double x, double y) const noexcept;
    friend struct FormulaParser;
};
} // namespace prism
