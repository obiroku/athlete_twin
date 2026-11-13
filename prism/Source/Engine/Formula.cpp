#include "Formula.h"
#include <cmath>
#include <cctype>
#include <cstring>

namespace prism
{
namespace
{
constexpr double PI = 3.14159265358979323846;
const char* kFuncs[] = { "sin", "cos", "tan", "abs", "sqrt", "pow", "exp", "log", "min", "max", "floor", "sign", "tri", "saw", "square", "rand" };
const int kArgs[] = { 1, 1, 1, 1, 1, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, -1 }; // -1: 0 or 1
constexpr int kNumFuncs = (int) (sizeof (kArgs) / sizeof (kArgs[0]));
constexpr int kMaxDepth = 64;
constexpr size_t kMaxNodes = 4096;

uint64_t hash64 (uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
double toUnit (uint64_t h) { return (double) (h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0; }
} // namespace

struct FormulaParser
{
    const std::string& s;
    size_t i = 0;
    int depth = 0;
    std::string err;
    Formula& f;

    FormulaParser (const std::string& str, Formula& fo) : s (str), f (fo) {}

    void ws() { while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i; }
    bool eat (char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    int node (Formula::Node n)
    {
        if (f.nodes.size() >= kMaxNodes) { if (err.empty()) err = "expression too long"; return -1; }
        f.nodes.push_back (n);
        return (int) f.nodes.size() - 1;
    }
    int bin (Formula::Node::Op op, int a, int b)
    {
        if (a < 0 || b < 0) return -1;
        Formula::Node n; n.op = op; n.a = a; n.b = b;
        return node (n);
    }

    int expr()
    {
        if (++depth > kMaxDepth) { err = "expression nested too deeply"; return -1; }
        int a = term();
        for (;;)
        {
            if (a < 0) break;
            if (eat ('+')) a = bin (Formula::Node::Add, a, term());
            else if (eat ('-')) a = bin (Formula::Node::Sub, a, term());
            else break;
        }
        --depth;
        return a;
    }
    int term()
    {
        int a = unary();
        for (;;)
        {
            if (a < 0) break;
            if (eat ('*')) a = bin (Formula::Node::Mul, a, unary());
            else if (eat ('/')) a = bin (Formula::Node::Div, a, unary());
            else if (eat ('%')) a = bin (Formula::Node::Mod, a, unary());
            else break;
        }
        return a;
    }
    int unary()
    {
        if (++depth > kMaxDepth) { err = "expression nested too deeply"; return -1; }
        int r;
        if (eat ('-')) { const int a = unary(); if (a < 0) { --depth; return -1; } Formula::Node n; n.op = Formula::Node::Neg; n.a = a; r = node (n); }
        else if (eat ('+')) r = unary();
        else
        {
            r = primary();
            if (r >= 0 && eat ('^')) r = bin (Formula::Node::Pow, r, unary());
        }
        --depth;
        return r;
    }
    int primary()
    {
        ws();
        if (i >= s.size()) { err = "unexpected end of formula"; return -1; }
        const char c = s[i];
        if (std::isdigit ((unsigned char) c) || c == '.')
        {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            const double v = std::strtod (start, &end);
            if (end == start) { err = "bad number"; return -1; }
            i += (size_t) (end - start);
            Formula::Node n; n.op = Formula::Node::Num; n.value = std::isfinite (v) ? v : 0.0;
            return node (n);
        }
        if (std::isalpha ((unsigned char) c))
        {
            size_t j = i;
            while (j < s.size() && std::isalnum ((unsigned char) s[j])) ++j;
            const std::string id = s.substr (i, j - i);
            i = j;
            if (id == "x") { Formula::Node n; n.op = Formula::Node::VarX; return node (n); }
            if (id == "y") { Formula::Node n; n.op = Formula::Node::VarY; return node (n); }
            if (id == "pi") { Formula::Node n; n.value = PI; return node (n); }
            if (id == "e") { Formula::Node n; n.value = std::exp (1.0); return node (n); }
            for (int k = 0; k < kNumFuncs; ++k)
                if (id == kFuncs[k])
                {
                    if (! eat ('(')) { err = "expected ( after " + id; return -1; }
                    Formula::Node n; n.op = Formula::Node::Func; n.func = (uint8_t) k;
                    if (kArgs[k] == -1 && eat (')')) { n.nargs = 0; return node (n); }
                    n.a = expr();
                    if (n.a < 0) return -1;
                    n.nargs = 1;
                    if (eat (','))
                    {
                        n.b = expr();
                        if (n.b < 0) return -1;
                        n.nargs = 2;
                    }
                    if (! eat (')')) { err = "expected ) after arguments of " + id; return -1; }
                    if (kArgs[k] > 0 && n.nargs != kArgs[k]) { err = id + " takes " + std::to_string (kArgs[k]) + " argument(s)"; return -1; }
                    if (kArgs[k] == -1 && n.nargs > 1) { err = "rand takes 0 or 1 argument"; return -1; }
                    return node (n);
                }
            err = "unknown name '" + id + "'";
            return -1;
        }
        if (eat ('('))
        {
            const int a = expr();
            if (a < 0) return -1;
            if (! eat (')')) { err = "missing )"; return -1; }
            return a;
        }
        err = std::string ("unexpected character '") + c + "'";
        return -1;
    }
};

std::unique_ptr<Formula> Formula::parse (const std::string& text, std::string& error)
{
    auto f = std::unique_ptr<Formula> (new Formula());
    if (text.size() > 2000) { error = "formula too long"; return nullptr; }
    FormulaParser p (text, *f);
    f->root = p.expr();
    p.ws();
    if (f->root < 0 || ! p.err.empty()) { error = p.err.empty() ? "parse error" : p.err; return nullptr; }
    if (p.i != text.size()) { error = "unexpected text at position " + std::to_string (p.i + 1); return nullptr; }
    return f;
}

double Formula::evalNode (int n, double x, double y) const noexcept
{
    const Node& d = nodes[(size_t) n];
    auto A = [&] { return evalNode (d.a, x, y); };
    auto B = [&] { return evalNode (d.b, x, y); };
    double r = 0.0;
    switch (d.op)
    {
        case Node::Num: r = d.value; break;
        case Node::VarX: r = x; break;
        case Node::VarY: r = y; break;
        case Node::Add: r = A() + B(); break;
        case Node::Sub: r = A() - B(); break;
        case Node::Mul: r = A() * B(); break;
        case Node::Div: { const double b = B(); r = b != 0.0 ? A() / b : 0.0; break; }
        case Node::Mod: { const double b = B(); r = b != 0.0 ? std::fmod (A(), b) : 0.0; break; }
        case Node::Pow: r = std::pow (A(), B()); break;
        case Node::Neg: r = -A(); break;
        case Node::Func:
        {
            const double a = d.nargs > 0 ? A() : 0.0;
            switch (d.func)
            {
                case 0: r = std::sin (a); break;
                case 1: r = std::cos (a); break;
                case 2: r = std::tan (a); break;
                case 3: r = std::abs (a); break;
                case 4: r = a > 0 ? std::sqrt (a) : 0.0; break;
                case 5: r = std::pow (a, B()); break;
                case 6: r = std::exp (std::min (a, 700.0)); break;
                case 7: r = a > 0 ? std::log (a) : 0.0; break;
                case 8: r = std::min (a, B()); break;
                case 9: r = std::max (a, B()); break;
                case 10: r = std::floor (a); break;
                case 11: r = a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0); break;
                case 12: r = std::isfinite (a) ? 2.0 / PI * std::asin (std::sin (a)) : 0.0; break;            // period 2 pi
                case 13: { const double t = a / (2 * PI) + 0.5; r = std::isfinite (t) ? 2.0 * (t - std::floor (t)) - 1.0 : 0.0; break; }
                case 14: { const double sn = std::sin (a); r = sn > 0 ? 1.0 : (sn < 0 ? -1.0 : 0.0); break; }
                case 15:
                    if (d.nargs == 0) { randState = hash64 (randState); r = toUnit (randState); }
                    else r = std::isfinite (a) ? toUnit (hash64 ((uint64_t) (int64_t) std::floor (a) ^ 0xA5A5ull)) : 0.0;
                    break;
                default: r = 0.0; break;
            }
            break;
        }
    }
    return std::isfinite (r) ? r : 0.0;
}

double Formula::eval (double x, double y) const noexcept
{
    if (root < 0) return 0.0;
    const double r = evalNode (root, x, y);
    return std::isfinite (r) ? r : 0.0;
}

std::vector<std::vector<float>> Formula::render (int frames, uint64_t seed) const
{
    frames = std::max (1, std::min (256, frames));
    randState = seed;
    std::vector<std::vector<float>> out ((size_t) frames, std::vector<float> (2048));
    for (int f = 0; f < frames; ++f)
    {
        const double y = frames > 1 ? (double) f / (frames - 1) : 0.0;
        for (int i = 0; i < 2048; ++i)
        {
            double v = eval ((double) i / 2048.0, y);
            if (! std::isfinite (v)) v = 0.0;
            out[(size_t) f][(size_t) i] = (float) std::max (-1e6, std::min (1e6, v));
        }
    }
    return out;
}
} // namespace prism
