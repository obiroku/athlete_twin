#include "Views.h"
#include "../DSP/FFT.h"
#include "../DSP/WavetableOsc.h"

namespace prism::gui
{
// Display-side copy of the phase warps (matches WavetableOsc's warpPhase)
static float displayWarp (float p, int mode, float a)
{
    auto bend = [] (float x, float b) { return x * (1.0f + b) / (1.0f + b * x); };
    switch ((WarpMode) mode)
    {
        case WarpMode::Sync: { float x = p * (1.0f + 7.0f * a); return x - std::floor (x); }
        case WarpMode::BendPlus: return bend (p, 7.0f * a);
        case WarpMode::BendMinus: return 1.0f - bend (1.0f - p, 7.0f * a);
        case WarpMode::BendBoth: return p < 0.5f ? 0.5f * bend (2 * p, 7 * a) : 1.0f - 0.5f * bend (2 - 2 * p, 7 * a);
        case WarpMode::PWM: { const float w = 1.0f - 0.98f * a; return p < w ? p / w : 0.0f; }
        case WarpMode::Mirror: { const float m = p < 0.5f ? 2 * p : 2 - 2 * p; return p + (m * 0.999f - p) * a; }
        case WarpMode::AsymPlus: case WarpMode::AsymMinus:
        {
            const float c = mode == (int) WarpMode::AsymPlus ? 0.5f - 0.49f * a : 0.5f + 0.49f * a;
            return p < c ? 0.5f * p / c : 0.5f + 0.5f * (p - c) / (1.0f - c);
        }
        case WarpMode::Quantize:
        {
            const float q = std::round (std::pow (2.0f, 1.0f + 7.0f * (1.0f - a)));
            return a > 0 ? std::floor (p * q) / q : p;
        }
        case WarpMode::Remap: { dsp::RemapCurve rc; return juce::jlimit (0.0f, 0.999f, p + (rc.eval (p) - p) * a); }
        default: return p;
    }
}

// ============================================================== wavetable view

WavetableView::WavetableView (PrismProcessor& p, int o) : proc (p), osc (o)
{
    setTooltip ("Drag to scrub the wavetable position");
    setTitle (o == 0 ? "Oscillator A wavetable" : "Oscillator B wavetable");
}

void WavetableView::rebuildPath()
{
    cached.clear();
    auto r = getLocalBounds().toFloat().reduced (6, 10);
    if (frames == nullptr || frames->empty() || r.isEmpty()) return;
    const int F = (int) frames->size();
    const float fpos = juce::jlimit (0.0f, 1.0f, shownPos) * (float) (F - 1);
    const int f0 = std::min (F - 1, (int) fpos), f1 = std::min (F - 1, f0 + 1);
    const float u = fpos - (float) f0;
    const auto& A = (*frames)[(size_t) f0];
    const auto& B = (*frames)[(size_t) f1];
    const int pts = std::max (64, (int) r.getWidth());
    for (int i = 0; i <= pts; ++i)
    {
        const float ph = displayWarp ((float) i / (float) pts, shownMode, shownWarp);
        const float x = ph * 2048.0f;
        int k = (int) x; const float fr = x - (float) k; k &= 2047;
        const int k1 = (k + 1) & 2047;
        float v = A[(size_t) k] + (A[(size_t) k1] - A[(size_t) k]) * fr;
        const float w = B[(size_t) k] + (B[(size_t) k1] - B[(size_t) k]) * fr;
        v += (w - v) * u;
        const float px = r.getX() + r.getWidth() * (float) i / (float) pts;
        const float py = r.getCentreY() - v * r.getHeight() * 0.5f / 0.9f;
        if (i == 0) cached.startNewSubPath (px, py); else cached.lineTo (px, py);
    }
}

void WavetableView::refreshLive()
{
    auto f = proc.tables.displayFrames (osc);
    bool dirty = f != frames;
    frames = f;
    float pos = proc.param (P::osc (osc, P::wt_pos))->getValue();
    if (proc.engine.voiceCount.load() > 0) pos = proc.engine.playhead[osc].load();
    const int mode = (int) std::lround (proc.param (P::osc (osc, P::warp_mode))->convertFrom0to1 (proc.param (P::osc (osc, P::warp_mode))->getValue()));
    const float warp = proc.param (P::osc (osc, P::warp_amt))->getValue();
    if (std::abs (pos - shownPos) > 0.001f || mode != shownMode || std::abs (warp - shownWarp) > 0.001f) dirty = true;
    if (dirty)
    {
        shownPos = pos; shownMode = mode; shownWarp = warp;
        rebuildPath();
        repaint();
    }
}

void WavetableView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4, r.getRight() - 4);
    if (cached.isEmpty())
    {
        g.setColour (col::dim);
        g.setFont (uiFont (12.0f));
        g.drawText ("generating table...", r, juce::Justification::centred);
        return;
    }
    g.setColour (col::accent.withAlpha (0.15f));
    juce::Path fill (cached);
    fill.lineTo (r.getRight() - 6, r.getCentreY());
    fill.lineTo (r.getX() + 6, r.getCentreY());
    fill.closeSubPath();
    g.fillPath (fill);
    g.setColour (col::accent);
    g.strokePath (cached, juce::PathStrokeType (1.8f));
    // playhead bar for WT position
    const float x = r.getX() + 4 + (r.getWidth() - 8) * juce::jlimit (0.0f, 1.0f, shownPos);
    g.setColour (col::text.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (r.getX() + 4, r.getBottom() - 4, r.getWidth() - 8, 2.0f));
    g.setColour (col::accent);
    g.fillRect (juce::Rectangle<float> (x - 3, r.getBottom() - 6, 6, 6));
    g.setColour (col::dim);
    g.setFont (uiFont (11.0f));
    if (frames) g.drawText (juce::String ((int) std::lround (shownPos * (float) (frames->size() - 1)) + 1) + "/" + juce::String ((int) frames->size()),
                            r.reduced (6, 2), juce::Justification::topRight);
}

void WavetableView::mouseDown (const juce::MouseEvent&)
{
    auto* prm = proc.param (P::osc (osc, P::wt_pos));
    dragStart = prm->getValue();
    prm->beginChangeGesture();
}

void WavetableView::mouseDrag (const juce::MouseEvent& e)
{
    auto* prm = proc.param (P::osc (osc, P::wt_pos));
    prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + (float) e.getDistanceFromDragStartX() / (float) std::max (1, getWidth())));
}

void WavetableView::mouseUp (const juce::MouseEvent&)
{
    proc.param (P::osc (osc, P::wt_pos))->endChangeGesture();
}

// ============================================================== shape editor

ShapeEditor::ShapeEditor (PrismProcessor& p, Target t, int i) : proc (p), target (t), index (i)
{
    setTitle (t == Target::Mseg ? "MSEG editor" : (t == Target::Lfo ? "LFO shape editor" : "Remap curve editor"));
    setDescription ("Drag points, double-click to add, right-click to delete, Alt-drag a segment to bend it");
    shape = get();
}

dsp::Shape ShapeEditor::get() const
{
    const auto x = proc.getExtras();
    switch (target)
    {
        case Target::Mseg: return x.mseg;
        case Target::Lfo: return x.lfoCustom[(size_t) index];
        default: return x.remap[(size_t) index];
    }
}

void ShapeEditor::set (const dsp::Shape& s)
{
    auto x = proc.getExtras();
    switch (target)
    {
        case Target::Mseg: x.mseg = s; break;
        case Target::Lfo: x.lfoCustom[(size_t) index] = s; break;
        default: x.remap[(size_t) index] = s; break;
    }
    proc.setExtras (x);
    seen = proc.extrasVersion.load();
}

juce::Point<float> ShapeEditor::toScreen (float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced (8);
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

int ShapeEditor::hit (juce::Point<float> p) const
{
    for (int i = 0; i < shape.count; ++i)
        if (toScreen (shape.pts[(size_t) i].x, shape.pts[(size_t) i].y).getDistanceFrom (p) < 8.0f) return i;
    return -1;
}

void ShapeEditor::refreshLive()
{
    const int v = proc.extrasVersion.load();
    if (v != seen && dragPt < 0 && bendSeg < 0) { seen = v; shape = get(); repaint(); }
}

void ShapeEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 5.0f);
    auto in = r.reduced (8);
    g.setColour (col::line);
    for (int i = 1; i < 4; ++i) g.drawVerticalLine ((int) (in.getX() + in.getWidth() * i / 4), in.getY(), in.getBottom());
    if (target == Target::Mseg)
    {
        const auto a = toScreen (shape.pts[(size_t) shape.loopStart].x, 0), b = toScreen (shape.pts[(size_t) shape.loopEnd].x, 0);
        g.setColour (col::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a.x, in.getY(), b.x, in.getBottom()));
    }
    juce::Path p;
    const int N = std::max (64, (int) in.getWidth());
    int seg = 0;
    for (int i = 0; i <= N; ++i)
    {
        const float x = (float) i / N;
        const auto s = toScreen (x, shape.eval (x, seg));
        if (i == 0) p.startNewSubPath (s); else p.lineTo (s);
    }
    g.setColour (col::accent);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    for (int i = 0; i < shape.count; ++i)
    {
        const auto s = toScreen (shape.pts[(size_t) i].x, shape.pts[(size_t) i].y);
        g.setColour (i == dragPt ? col::text : col::accent);
        g.fillEllipse (s.x - 4, s.y - 4, 8, 8);
    }
}

void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    shape = get();
    const int h = hit (e.position);
    if (e.mods.isPopupMenu())
    {
        if (h > 0 && h < shape.count - 1)
        {
            for (int i = h; i < shape.count - 1; ++i) shape.pts[(size_t) i] = shape.pts[(size_t) i + 1];
            --shape.count;
            shape.sanitize();
            set (shape);
            repaint();
        }
        else if (target == Target::Mseg)
        {
            juce::PopupMenu m;
            for (int i = 0; i < shape.count - 1; ++i) m.addItem (100 + i, "Loop start at point " + juce::String (i + 1), true, i == shape.loopStart);
            m.addSeparator();
            for (int i = 1; i < shape.count; ++i) m.addItem (200 + i, "Loop end at point " + juce::String (i + 1), true, i == shape.loopEnd);
            juce::Component::SafePointer<ShapeEditor> safe (this);
            m.showMenuAsync ({}, [safe] (int r) {
                if (safe == nullptr || r == 0) return;
                auto s = safe->get();
                if (r >= 200) s.loopEnd = r - 200; else s.loopStart = r - 100;
                s.sanitize();
                safe->set (s);
                safe->shape = s;
                safe->repaint();
            });
        }
        return;
    }
    if (e.mods.isAltDown())
    {
        auto r = getLocalBounds().toFloat().reduced (8);
        const float x = (e.position.x - r.getX()) / r.getWidth();
        for (int i = 1; i < shape.count; ++i)
            if (x <= shape.pts[(size_t) i].x) { bendSeg = i; bendStart = shape.pts[(size_t) i].curve; break; }
        return;
    }
    dragPt = h;
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    auto r = getLocalBounds().toFloat().reduced (8);
    if (bendSeg > 0)
    {
        shape.pts[(size_t) bendSeg].curve = juce::jlimit (-1.0f, 1.0f, bendStart + (float) e.getDistanceFromDragStartY() / 100.0f);
        repaint();
        return;
    }
    if (dragPt < 0) return;
    auto& pt = shape.pts[(size_t) dragPt];
    float x = (e.position.x - r.getX()) / r.getWidth();
    const float y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
    if (dragPt == 0) x = 0.0f;
    else if (dragPt == shape.count - 1) x = 1.0f;
    else x = juce::jlimit (shape.pts[(size_t) dragPt - 1].x, shape.pts[(size_t) dragPt + 1].x, x);
    pt.x = x; pt.y = y;
    repaint();
}

void ShapeEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragPt >= 0 || bendSeg > 0) { shape.sanitize(); set (shape); }
    dragPt = -1; bendSeg = -1;
    repaint();
}

void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hit (e.position) >= 0 || shape.count >= dsp::Shape::MaxPoints) return;
    auto r = getLocalBounds().toFloat().reduced (8);
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
    int at = 1;
    while (at < shape.count && shape.pts[(size_t) at].x < x) ++at;
    for (int i = shape.count; i > at; --i) shape.pts[(size_t) i] = shape.pts[(size_t) i - 1];
    shape.pts[(size_t) at] = { x, y, 0.0f };
    ++shape.count;
    if (shape.loopEnd >= at) ++shape.loopEnd;
    if (shape.loopStart >= at) ++shape.loopStart;
    shape.sanitize();
    set (shape);
    repaint();
}

// ============================================================== scope / meter

void ScopeView::refreshLive()
{
    const int w = proc.scopeWrite.load (std::memory_order_acquire);
    if (w == lastW) return;
    lastW = w;
    for (int i = 0; i < (int) snap.size(); ++i)
        snap[(size_t) i] = proc.scope[(size_t) ((w - (int) snap.size() + i + PrismProcessor::ScopeSize) & (PrismProcessor::ScopeSize - 1))];
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    juce::Path p;
    if (! spectrum)
    {
        for (size_t i = 0; i < snap.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) snap.size();
            const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, snap[i]) * r.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
    }
    else
    {
        std::vector<dsp::cplx> a (snap.size());
        for (size_t i = 0; i < snap.size(); ++i)
            a[i] = snap[i] * (0.5 - 0.5 * std::cos (2.0 * dsp::kPi * (double) i / (double) snap.size()));
        dsp::fft (a, false);
        bool first = true;
        for (int k = 1; k < (int) snap.size() / 2; ++k)
        {
            const float fx = std::log ((float) k) / std::log ((float) snap.size() / 2.0f);
            const float db = 20.0f * std::log10 ((float) std::abs (a[(size_t) k]) / 128.0f + 1e-9f);
            const float y = r.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * r.getHeight();
            const float x = r.getX() + fx * r.getWidth();
            if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y);
        }
    }
    g.setColour (col::accent);
    g.strokePath (p, juce::PathStrokeType (1.2f));
    g.setColour (col::dim);
    g.setFont (uiFont (10.0f));
    g.drawText (spectrum ? "spectrum" : "scope", r.reduced (4, 2), juce::Justification::topLeft);
}

void Meter::refreshLive()
{
    const float nl = proc.engine.meterL.exchange (0.0f), nr = proc.engine.meterR.exchange (0.0f);
    const float tl = std::max (nl, l * 0.85f), tr = std::max (nr, r * 0.85f);
    if (std::abs (tl - l) > 0.002f || std::abs (tr - r) > 0.002f) { l = tl; r = tr; repaint(); }
}

void Meter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (b, 3.0f);
    auto bar = [&] (juce::Rectangle<float> rr, float v) {
        const float db = juce::jlimit (-60.0f, 0.0f, 20.0f * std::log10 (v + 1e-9f));
        const float frac = (db + 60.0f) / 60.0f;
        g.setColour (db > -1.5f ? juce::Colour (0xffe05a4a) : col::accent);
        g.fillRect (rr.withWidth (rr.getWidth() * frac));
    };
    auto top = b.reduced (2).removeFromTop (b.getHeight() * 0.5f - 2);
    bar (top, l);
    bar (b.reduced (2).removeFromBottom (b.getHeight() * 0.5f - 2), r);
}
} // namespace prism::gui
