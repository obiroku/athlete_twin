#include "SpectrumDisplay.h"

#include "Coach/CoachContent.h"
#include "Params/Parameters.h"
#include "DSP/Spectral.h"
#include "Util/Format.h"

namespace lens::gui
{
namespace
{
namespace pb = params::band;
constexpr float dotRadius = 9.0f;

juce::StringArray shapeNames() { return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt", "All Pass" }; }
juce::StringArray stereoNames() { return { "Stereo", "Mid", "Side", "Left", "Right" }; }

void styleToggle (juce::ToggleButton& b, const juce::String& tip)
{
    b.setClickingTogglesState (true);
    b.setTooltip (tip);
}
} // namespace

SpectrumDisplay::SpectrumDisplay (EditorContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTitle ("Spectrum display");
    setDescription (coach::Content::get().tooltip ("Peak marker"));
    ctx.selection.addChangeListener (this);

    const auto s = ctx.proc.getSession();
    for (auto* b : { &preBtn, &postBtn, &scBtn, &freezeBtn })
    {
        addAndMakeVisible (*b);
        styleToggle (*b, {});
        b->onClick = [this] { syncControls(); };
    }
    preBtn.setToggleState (s.showPre, juce::dontSendNotification);
    postBtn.setToggleState (s.showPost, juce::dontSendNotification);
    scBtn.setToggleState (s.showSidechain, juce::dontSendNotification);
    preBtn.setTitle ("Show pre-EQ trace");
    postBtn.setTitle ("Show post-EQ trace");
    scBtn.setTitle ("Show side-chain trace");
    freezeBtn.setTitle ("Freeze analyzer");

    smoothBox.addItemList ({ "Raw", "1/12 oct", "1/6 oct", "1/3 oct" }, 1);
    speedBox.addItemList ({ "Slow", "Medium", "Fast", "Very fast" }, 1);
    tiltBox.addItemList ({ "Tilt 0", "Tilt 3", "Tilt 4.5" }, 1);
    rangeBox.addItemList ({ juce::String::fromUTF8 ("\xc2\xb1") + "12 dB", juce::String::fromUTF8 ("\xc2\xb1") + "24 dB", juce::String::fromUTF8 ("\xc2\xb1") + "30 dB" }, 1);
    smoothBox.setSelectedItemIndex (state::SessionState::smoothings().indexOf (s.analyzerSmoothing), juce::dontSendNotification);
    speedBox.setSelectedItemIndex (state::SessionState::speeds().indexOf (s.analyzerSpeed), juce::dontSendNotification);
    tiltBox.setSelectedItemIndex (s.analyzerTilt < 1.0 ? 0 : (s.analyzerTilt < 4.0 ? 1 : 2), juce::dontSendNotification);
    rangeBox.setSelectedItemIndex (1, juce::dontSendNotification);
    smoothBox.setTitle ("Analyzer smoothing");
    speedBox.setTitle ("Analyzer speed");
    tiltBox.setTitle ("Analyzer display tilt");
    rangeBox.setTitle ("EQ display range");
    for (auto* b : { &smoothBox, &speedBox, &tiltBox, &rangeBox })
    {
        addAndMakeVisible (*b);
        b->onChange = [this] { syncControls(); };
    }
    syncControls();
}

SpectrumDisplay::~SpectrumDisplay()
{
    ctx.selection.removeChangeListener (this);
}

void SpectrumDisplay::syncControls()
{
    auto s = ctx.proc.getSession();
    s.showPre = preBtn.getToggleState();
    s.showPost = postBtn.getToggleState();
    s.showSidechain = scBtn.getToggleState();
    s.analyzerSmoothing = state::SessionState::smoothings()[juce::jmax (0, smoothBox.getSelectedItemIndex())];
    s.analyzerSpeed = state::SessionState::speeds()[juce::jmax (0, speedBox.getSelectedItemIndex())];
    s.analyzerTilt = std::array<double, 3> { 0.0, 3.0, 4.5 }[(size_t) juce::jmax (0, tiltBox.getSelectedItemIndex())];
    ctx.proc.setSession (s);

    ctx.analyzer.smoothing = juce::jmax (0, smoothBox.getSelectedItemIndex());
    ctx.analyzer.speed = juce::jmax (0, speedBox.getSelectedItemIndex());
    ctx.analyzer.tilt = (float) s.analyzerTilt;
    ctx.analyzer.frozen = freezeBtn.getToggleState();
    eqRange = std::array<double, 3> { 12.0, 24.0, 30.0 }[(size_t) juce::jmax (0, rangeBox.getSelectedItemIndex())];
    repaint();
}

void SpectrumDisplay::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromTop (26).reduced (6, 3);
    for (auto* b : { &preBtn, &postBtn, &scBtn })
    {
        b->setBounds (row.removeFromLeft (42));
        row.removeFromLeft (4);
    }
    row.removeFromLeft (6);
    smoothBox.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    speedBox.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (4);
    tiltBox.setBounds (row.removeFromLeft (72));
    row.removeFromLeft (4);
    freezeBtn.setBounds (row.removeFromLeft (58));
    rangeBox.setBounds (row.removeFromRight (74));

    auto area = r.toFloat();
    pianoStrip = area.removeFromBottom (20.0f);
    zoneStrip = area.removeFromBottom (16.0f);
    plot = area.withTrimmedLeft (30.0f).withTrimmedRight (30.0f).withTrimmedTop (4.0f).withTrimmedBottom (2.0f);
    pianoStrip = pianoStrip.withLeft (plot.getX()).withRight (plot.getRight());
    zoneStrip = zoneStrip.withLeft (plot.getX()).withRight (plot.getRight());
}

// ---------------------------------------------------------------- mapping

float SpectrumDisplay::freqToX (double f) const
{
    return plot.getX() + plot.getWidth() * (float) (std::log (f / fMin) / std::log (fMax / fMin));
}

double SpectrumDisplay::xToFreq (float x) const
{
    return fMin * std::pow (fMax / fMin, (double) ((x - plot.getX()) / plot.getWidth()));
}

float SpectrumDisplay::gainToY (double db) const
{
    return plot.getCentreY() - (float) (db / eqRange) * plot.getHeight() * 0.5f;
}

double SpectrumDisplay::yToGain (float y) const
{
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * eqRange;
}

float SpectrumDisplay::analyzerToY (double db) const
{
    const double lo = SpectrumAnalyzer::minDisplayDb, hi = SpectrumAnalyzer::maxDisplayDb;
    return plot.getBottom() - (float) ((db - lo) / (hi - lo)) * plot.getHeight();
}

// ---------------------------------------------------------------- data

dsp::BandDesign SpectrumDisplay::designFor (const dsp::BandParams& p, double gainDb) const
{
    if (linearPhase)
        return dsp::designAnalog (p.shape, p.freq, gainDb, p.q, p.slope);
    return dsp::designBand (p.shape, p.freq, gainDb, p.q, p.slope, fs);
}

void SpectrumDisplay::gatherBands()
{
    dsp::EngineParams ep;
    ctx.proc.readParams (ep);
    linearPhase = ep.global.linearPhase;
    fs = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    fMax = std::min (30000.0, fs * 0.5);

    bands.clear();
    for (int b = 0; b < dsp::kNumBands; ++b)
    {
        const auto& p = ep.bands[(size_t) b];
        if (! p.on)
            continue;
        BandView v;
        v.index = b;
        v.p = p;
        v.dynGainDb = p.isDynamic() ? ctx.proc.stats().bands[(size_t) b].dynGainDb.load() : 0.0f;
        const float x = freqToX (juce::jlimit (fMin, fMax, (double) p.freq));
        const float y = dsp::usesGain (p.shape) ? gainToY (juce::jlimit (-eqRange, eqRange, (double) p.gain)) : gainToY (0.0);
        v.pos = { x, y };
        bands.push_back (v);
    }
}

double SpectrumDisplay::curveDbAt (double f) const
{
    std::complex<double> h (1.0, 0.0);
    for (const auto& b : bands)
        h *= linearPhase ? dsp::responseAnalog (designFor (b.p, b.p.gain + b.dynGainDb), f)
                         : dsp::responseDigital (designFor (b.p, b.p.gain + b.dynGainDb), f, fs);
    return dsp::toDb (std::abs (h));
}

void SpectrumDisplay::refresh()
{
    gatherBands();

    juce::String hash;
    for (const auto& b : bands)
        hash << b.index << ':' << (int) b.p.shape << ':' << b.p.freq << ':' << b.p.gain << ':' << b.p.q << ':' << b.p.slope << ':' << (int) b.p.stereo
             << ':' << (int) b.p.dynOn << ';';
    hash << (linearPhase ? "LP" : "ZL") << ctx.proc.getSession().trackType;

    bool changed = hash != paramsHash;
    paramsHash = hash;

    for (const auto& b : bands)
        if (std::abs (b.dynGainDb - lastDyn[(size_t) b.index]) > 0.05f)
        {
            lastDyn[(size_t) b.index] = b.dynGainDb;
            changed = true;
        }

    const int v = ctx.analyzer.analyzer().version();
    if (v != lastAnalyzerVersion)
    {
        lastAnalyzerVersion = v;
        ctx.analyzer.analyzer().getSnapshot (snapshot);
        peakMarkers = snapshot.peaks;
        changed = true;
    }

    if (changed)
        repaint();
}

int SpectrumDisplay::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = dotRadius + 3.0f;
    for (const auto& b : bands)
    {
        const float d = b.pos.getDistanceFrom (p);
        if (d < bestD)
        {
            bestD = d;
            best = b.index;
        }
    }
    return best;
}

int SpectrumDisplay::peakAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < peakMarkers.size(); ++i)
    {
        const float x = freqToX (peakMarkers[i].freq);
        if (std::abs (p.x - x) < 8.0f && p.y >= plot.getY() && p.y < plot.getY() + 18.0f)
            return (int) i;
    }
    return -1;
}

// ---------------------------------------------------------------- painting

void SpectrumDisplay::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::background);
    g.fillRect (plot);

    drawGrid (g);
    drawAnalyzer (g);
    drawCurve (g);
    drawPeaks (g);
    drawBands (g);
    drawStrips (g);
    drawHover (g);

    if (earTrainerActive)
    {
        g.setColour (theme::accent);
        g.setFont (theme::font (22.0f, true));
        g.drawText ("EAR TRAINER ON", plot.withHeight (40.0f).translated (0.0f, 20.0f), juce::Justification::centred);
    }
}

void SpectrumDisplay::drawGrid (juce::Graphics& g)
{
    g.setFont (theme::font (10.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        if (f > fMax)
            continue;
        const float x = freqToX (f);
        g.setColour (theme::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (theme::textDim);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 15, plot.getBottom() - 13, 30, 12),
                    juce::Justification::centred);
    }
    const double step = eqRange <= 12.0 ? 3.0 : 6.0;
    for (double db = -eqRange; db <= eqRange + 1e-9; db += step)
    {
        const float y = gainToY (db);
        g.setColour (std::abs (db) < 1e-9 ? theme::outline : theme::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (theme::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 30, y - 6, 26, 12), juce::Justification::centredRight);
    }
    for (double db = -80.0; db <= 20.0; db += 20.0)
    {
        g.setColour (theme::textDim.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 3, analyzerToY (db) - 6, 26, 12), juce::Justification::centredLeft);
    }
}

void SpectrumDisplay::drawAnalyzer (juce::Graphics& g)
{
    const auto& f = snapshot.freqs;
    if (f.empty())
        return;

    auto pathFor = [&] (const std::vector<float>& db, bool closed)
    {
        juce::Path p;
        bool started = false;
        for (size_t i = 0; i < f.size() && i < db.size(); ++i)
        {
            if (f[i] < fMin || f[i] > fMax)
                continue;
            const float x = freqToX (f[i]);
            const float y = juce::jlimit (plot.getY(), plot.getBottom(), analyzerToY (db[i]));
            if (! started)
            {
                if (closed)
                {
                    p.startNewSubPath (x, plot.getBottom());
                    p.lineTo (x, y);
                }
                else
                    p.startNewSubPath (x, y);
                started = true;
            }
            else
                p.lineTo (x, y);
        }
        if (closed && started)
        {
            p.lineTo (plot.getRight(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    if (preBtn.getToggleState())
    {
        g.setColour (juce::Colour (0xff8a8c90).withAlpha (0.28f));
        g.fillPath (pathFor (snapshot.db[SpectrumAnalyzer::Pre], true));
    }
    if (postBtn.getToggleState())
    {
        g.setColour (theme::accent.withAlpha (0.55f));
        g.strokePath (pathFor (snapshot.db[SpectrumAnalyzer::Post], false), juce::PathStrokeType (1.2f));
    }
    bool external = false;
    for (const auto& b : bands)
        external = external || (b.p.isDynamic() && b.p.detExternal);
    const bool scArriving = ctx.proc.stats().sidechainLevelDb.load() > -70.0f;
    if (scBtn.getToggleState() && external && scArriving)
    {
        g.setColour (theme::text.withAlpha (0.45f));
        g.strokePath (pathFor (snapshot.db[SpectrumAnalyzer::Sidechain], false), juce::PathStrokeType (0.8f));
    }
    g.restoreState();
}

void SpectrumDisplay::drawCurve (juce::Graphics& g)
{
    if (bands.empty())
        return;
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    const int n = juce::jmax (64, (int) plot.getWidth() / 2);
    std::vector<dsp::BandDesign> designs;
    designs.reserve (bands.size());
    for (const auto& b : bands)
        designs.push_back (designFor (b.p, b.p.gain + b.dynGainDb));

    juce::Path total;
    const int sel = ctx.selection.primary();
    juce::Path selected;
    for (int i = 0; i <= n; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) n;
        const double f = xToFreq (x);
        std::complex<double> h (1.0, 0.0);
        for (size_t k = 0; k < bands.size(); ++k)
        {
            const auto r = linearPhase ? dsp::responseAnalog (designs[k], f) : dsp::responseDigital (designs[k], f, fs);
            h *= r;
            if (bands[k].index == sel)
            {
                const float y = gainToY (juce::jlimit (-eqRange * 1.5, eqRange * 1.5, dsp::toDb (std::abs (r))));
                if (i == 0)
                    selected.startNewSubPath (x, gainToY (0));
                selected.lineTo (x, y);
            }
        }
        const float y = gainToY (juce::jlimit (-eqRange * 1.5, eqRange * 1.5, dsp::toDb (std::abs (h))));
        if (i == 0)
            total.startNewSubPath (x, y);
        else
            total.lineTo (x, y);
    }
    if (! selected.isEmpty())
    {
        selected.lineTo (plot.getRight(), gainToY (0));
        selected.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillPath (selected);
    }
    // Spectral dynamics: frequencies being pulled down, as a darker region.
    bool anySpectral = false;
    for (const auto& b : bands)
        anySpectral = anySpectral || b.p.isSpectral();
    if (anySpectral)
    {
        auto& sp = ctx.proc.getEngine().spectral();
        for (int i = 0; i < n; ++i)
        {
            const float x0 = plot.getX() + plot.getWidth() * (float) i / (float) n;
            const float x1 = plot.getX() + plot.getWidth() * (float) (i + 1) / (float) n;
            const float cut = sp.publishedCutDb (xToFreq (0.5f * (x0 + x1)));
            if (cut > 0.5f)
            {
                g.setColour (theme::background.withAlpha (juce::jlimit (0.15f, 0.7f, cut / 24.0f + 0.15f)));
                g.fillRect (juce::Rectangle<float> (x0, gainToY (0.0), x1 - x0, gainToY (-cut) - gainToY (0.0)));
            }
        }
    }

    g.setColour (theme::accent);
    g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

    for (const auto& gh : ghosts)
    {
        juce::Path p;
        const auto d = dsp::designAnalog (dsp::Shape::Bell, gh.freq, gh.gain, gh.q, 1);
        for (int i = 0; i <= n; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) n;
            const float y = gainToY (dsp::toDb (std::abs (dsp::responseAnalog (d, xToFreq (x)))));
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (theme::text.withAlpha (0.7f));
        g.fillPath (dashed);
    }
    g.restoreState();
}

void SpectrumDisplay::drawBands (juce::Graphics& g)
{
    g.setFont (theme::font (10.5f, true));
    for (const auto& b : bands)
    {
        const bool sel = ctx.selection.contains (b.index);
        const auto c = b.pos;
        if (b.p.isDynamic())
        {
            // Ring meter: how far the band is moving toward its range.
            const float range = std::abs (b.p.dynRange) > 0.01f ? std::abs (b.p.dynRange) : 1.0f;
            const float amount = juce::jlimit (0.0f, 1.0f, std::abs (b.dynGainDb) / range);
            g.setColour (theme::outline);
            g.drawEllipse (c.x - dotRadius - 4, c.y - dotRadius - 4, (dotRadius + 4) * 2, (dotRadius + 4) * 2, 2.0f);
            if (amount > 0.001f)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, dotRadius + 4, dotRadius + 4, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * amount, true);
                g.setColour (b.dynGainDb < 0 ? theme::accent : theme::text);
                g.strokePath (arc, juce::PathStrokeType (2.5f));
            }
        }
        g.setColour (sel ? theme::accent : theme::dot);
        g.fillEllipse (c.x - dotRadius, c.y - dotRadius, dotRadius * 2, dotRadius * 2);
        g.setColour (theme::background);
        g.drawText (juce::String (b.index + 1), juce::Rectangle<float> (c.x - dotRadius, c.y - dotRadius, dotRadius * 2, dotRadius * 2), juce::Justification::centred);
    }
}

void SpectrumDisplay::drawPeaks (juce::Graphics& g)
{
    const bool hl = ctx.highlights.contains ("peak markers");
    for (const auto& pm : peakMarkers)
    {
        const float x = freqToX (pm.freq);
        juce::Path tri;
        tri.addTriangle (x - 6, plot.getY() + 2, x + 6, plot.getY() + 2, x, plot.getY() + 12);
        g.setColour (hl ? theme::accent : theme::text.withAlpha (0.8f));
        g.fillPath (tri);
        g.setColour (theme::text.withAlpha (0.25f));
        const float dashes[] = { 2.0f, 3.0f };
        g.drawDashedLine ({ x, plot.getY() + 12, x, plot.getBottom() }, dashes, 2, 1.0f);
    }
}

void SpectrumDisplay::drawStrips (juce::Graphics& g)
{
    // Zone strip for the current track type.
    const auto trackType = ctx.proc.getSession().trackType;
    const auto& zones = coach::Content::get().zonesFor (trackType);
    g.setFont (theme::font (10.0f));
    for (size_t i = 0; i < zones.size(); ++i)
    {
        const auto& z = zones[i];
        const double hi = z.hi > 0.0 ? z.hi : fMax;
        const float x0 = juce::jmax (zoneStrip.getX(), freqToX (z.lo));
        const float x1 = juce::jmin (zoneStrip.getRight(), freqToX (hi));
        if (x1 <= x0)
            continue;
        const auto r = juce::Rectangle<float> (x0, zoneStrip.getY(), x1 - x0, zoneStrip.getHeight()).reduced (0.5f, 1.0f);
        g.setColour (i % 2 == 0 ? theme::panelRaised : theme::panel.brighter (0.06f));
        g.fillRect (r);
        g.setColour (theme::textDim);
        g.drawFittedText (z.name, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    // Piano strip, A0 (MIDI 21) to C8 (MIDI 108), Logic octave numbers.
    g.setColour (theme::text.withAlpha (0.85f));
    g.fillRect (pianoStrip);
    for (int midi = 21; midi <= 108; ++midi)
    {
        const double f0 = 440.0 * std::pow (2.0, (midi - 69 - 0.5) / 12.0);
        const double f1 = 440.0 * std::pow (2.0, (midi - 69 + 0.5) / 12.0);
        const float x0 = freqToX (f0), x1 = freqToX (f1);
        const int pc = midi % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black)
        {
            g.setColour (theme::background);
            g.fillRect (juce::Rectangle<float> (x0, pianoStrip.getY(), x1 - x0, pianoStrip.getHeight() * 0.62f));
        }
        if (pc == 0)
        {
            g.setColour (theme::background.withAlpha (0.5f));
            g.drawVerticalLine ((int) x0, pianoStrip.getY(), pianoStrip.getBottom());
            g.setColour (theme::background);
            g.setFont (theme::font (9.0f));
            g.drawText (fmt::note (440.0 * std::pow (2.0, (midi - 69) / 12.0)).name, juce::Rectangle<float> (x0 + 1, pianoStrip.getBottom() - 10, 24, 10),
                        juce::Justification::centredLeft);
        }
    }
}

void SpectrumDisplay::drawHover (juce::Graphics& g)
{
    if (! plot.contains (hover))
        return;
    const double f = xToFreq (hover.x);
    juce::String text = fmt::freq (f) + "  " + fmt::noteWithCents (f);
    if (! snapshot.freqs.empty())
    {
        const auto trace = postBtn.getToggleState() ? SpectrumAnalyzer::Post : SpectrumAnalyzer::Pre;
        const double lvl = ctx.analyzer.analyzer().displayLevelAt (trace, f);
        if (lvl > SpectrumAnalyzer::minDisplayDb)
            text << "  " << fmt::number (lvl, 1) << " dB";
    }
    text << "  EQ " << fmt::gain (curveDbAt (f));
    g.setFont (theme::font (11.5f));
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 14.0f;
    auto r = juce::Rectangle<float> (hover.x + 12, hover.y - 24, w, 18);
    if (r.getRight() > plot.getRight())
        r.setX (hover.x - 12 - w);
    g.setColour (theme::panelRaised.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::text);
    g.drawText (text, r, juce::Justification::centred);
    g.setColour (theme::text.withAlpha (0.2f));
    g.drawVerticalLine ((int) hover.x, plot.getY(), plot.getBottom());
}

// ---------------------------------------------------------------- mouse

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    const int pk = peakAt (e.position);
    if (pk >= 0)
        setTooltip (coach::Content::get().tooltip ("Peak marker") + "\n" + fmt::freq (peakMarkers[(size_t) pk].freq));
    else
        setTooltip ({});
    repaint();
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hover = { -1, -1 };
    repaint();
}

void SpectrumDisplay::addBellAt (double freq)
{
    dsp::BandParams bp;
    bp.shape = dsp::Shape::Bell;
    bp.freq = (float) juce::jlimit (10.0, 30000.0, freq);
    bp.gain = 0.0f;
    bp.q = 1.0f;
    const int b = ctx.proc.addBand (bp);
    if (b >= 0)
    {
        ctx.selection.selectOnly (b);
        ctx.uiEvent ("band added");
    }
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragging = false;
    if (! plot.contains (e.position))
        return;

    if (earTrainerActive && onEarGuess)
    {
        onEarGuess (xToFreq (e.position.x));
        return;
    }

    const int pk = peakAt (e.position);
    if (pk >= 0 && ! e.mods.isPopupMenu())
    {
        const auto& m = peakMarkers[(size_t) pk];
        dsp::BandParams bp;
        bp.shape = dsp::Shape::Bell;
        bp.freq = (float) m.freq;
        bp.q = (float) juce::jlimit (2.0, 10.0, m.q);
        if (e.mods.isShiftDown())
        {
            bp.gain = -3.0f;
            const int b = ctx.proc.addBand (bp, "Cut resonance");
            ctx.selection.selectOnly (b);
        }
        else
        {
            bp.gain = 0.0f;
            bp.dynOn = true;
            bp.dynMode = dsp::DynMode::Above;
            bp.dynRange = -6.0f;
            const int b = ctx.proc.addBand (bp, "Tame resonance");
            if (b >= 0)
            {
                ctx.selection.selectOnly (b);
                ctx.proc.toggleLearn (b);
            }
        }
        ctx.uiEvent ("band added");
        return;
    }

    const int b = bandAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (b >= 0)
        {
            if (! ctx.selection.contains (b))
                ctx.selection.selectOnly (b);
            showBandMenu (b);
        }
        return;
    }

    if (b < 0)
    {
        if (! e.mods.isShiftDown())
            ctx.selection.clear();
        if (freezeBtn.getToggleState() && e.getNumberOfClicks() == 1)
            addBellAt (xToFreq (e.position.x));
        return;
    }

    if (e.mods.isShiftDown())
        ctx.selection.toggle (b);
    else if (! ctx.selection.contains (b))
        ctx.selection.selectOnly (b);
    else
    {
        // Make it primary, keep the multi-selection.
        auto s = ctx.selection.bands();
        s.removeFirstMatchingValue (b);
        s.insert (0, b);
        ctx.selection.set (s);
    }

    if (! ctx.selection.contains (b))
        return;

    dragStart.clear();
    for (int s : ctx.selection.bands())
        dragStart.push_back ({ s, ctx.proc.bandValue (s, pb::freq), ctx.proc.bandValue (s, pb::gain) });
    dragOrigin = e.position;
    dragging = true;
    ctx.proc.beginEdit();
    for (const auto& d : dragStart)
        for (auto* suffix : { pb::freq, pb::gain })
            ctx.proc.getApvts().getParameter (params::bandParamId (d.band + 1, suffix))->beginChangeGesture();

    soloDrag = e.mods.isAltDown();
    if (soloDrag)
        ctx.proc.soloBand = b;
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const double fine = e.mods.isCommandDown() ? 0.1 : 1.0;
    const double octPerPx = std::log2 (fMax / fMin) / plot.getWidth();
    const double dOct = (e.position.x - dragOrigin.x) * octPerPx * fine;
    const double dDb = (dragOrigin.y - e.position.y) / (plot.getHeight() * 0.5) * eqRange * fine;
    for (const auto& d : dragStart)
    {
        ctx.proc.setBandValue (d.band, pb::freq, (float) juce::jlimit (10.0, 30000.0, d.freq * std::pow (2.0, dOct)));
        const auto shape = (dsp::Shape) (int) ctx.proc.bandValue (d.band, pb::shape);
        if (dsp::usesGain (shape))
            ctx.proc.setBandValue (d.band, pb::gain, (float) juce::jlimit (-30.0, 30.0, d.gain + dDb));
    }
    hover = e.position;
    refresh();
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        for (const auto& d : dragStart)
            for (auto* suffix : { pb::freq, pb::gain })
                ctx.proc.getApvts().getParameter (params::bandParamId (d.band + 1, suffix))->endChangeGesture();
        ctx.proc.endEdit ("Move band");
        ctx.uiEvent ("band moved");
    }
    dragging = false;
    if (soloDrag)
        ctx.proc.soloBand = -1;
    soloDrag = false;
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (earTrainerActive || ! plot.contains (e.position) || bandAt (e.position) >= 0 || peakAt (e.position) >= 0)
        return;
    addBellAt (xToFreq (e.position.x));
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = bandAt (e.position);
    if (b < 0)
        return;
    auto* p = ctx.proc.getApvts().getParameter (params::bandParamId (b + 1, pb::q));
    const double q = ctx.proc.bandValue (b, pb::q);
    const double step = (w.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isCommandDown() ? 0.02 : 0.12);
    p->beginChangeGesture();
    ctx.proc.setBandValue (b, pb::q, (float) juce::jlimit (0.025, 40.0, q * std::pow (2.0, step)));
    p->endChangeGesture();
    ctx.uiEvent ("q changed");
}

bool SpectrumDisplay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        ctx.proc.removeBands (ctx.selection.bands());
        ctx.selection.clear();
        return true;
    }
    return false;
}

void SpectrumDisplay::showBandMenu (int band)
{
    juce::PopupMenu shapeMenu, stereoMenu, m;
    const int shape = (int) ctx.proc.bandValue (band, pb::shape);
    const int stereo = (int) ctx.proc.bandValue (band, pb::stereo);
    const auto names = shapeNames();
    for (int i = 0; i < names.size(); ++i)
        shapeMenu.addItem (100 + i, names[i], true, i == shape);
    const bool mono = ctx.proc.isMono();
    const auto snames = stereoNames();
    for (int i = 0; i < snames.size(); ++i)
        stereoMenu.addItem (200 + i, snames[i], ! mono || i == 0, i == stereo);

    m.addSubMenu ("Shape", shapeMenu);
    const bool canDyn = dsp::canBeDynamic ((dsp::Shape) shape);
    const bool isDyn = ctx.proc.bandValue (band, pb::dynOn) > 0.5f;
    m.addItem (1, isDyn ? "Make static" : "Make dynamic", canDyn);
    m.addSubMenu ("Stereo mode", stereoMenu);
    m.addItem (2, "Solo", true, ctx.proc.soloBand.load() == band);
    m.addSeparator();
    m.addItem (3, "Delete");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, band] (int r)
    {
        if (r == 0)
            return;
        auto gesture = [this, band] (const char* suffix, float v)
        {
            auto* p = ctx.proc.getApvts().getParameter (params::bandParamId (band + 1, suffix));
            p->beginChangeGesture();
            ctx.proc.setBandValue (band, suffix, v);
            p->endChangeGesture();
        };
        if (r >= 100 && r < 200)
            gesture (pb::shape, (float) (r - 100));
        else if (r >= 200)
            gesture (pb::stereo, (float) (r - 200));
        else if (r == 1)
        {
            const bool now = ctx.proc.bandValue (band, pb::dynOn) < 0.5f;
            gesture (pb::dynOn, now ? 1.0f : 0.0f);
            if (now)
                ctx.uiEvent ("made dynamic");
        }
        else if (r == 2)
            ctx.proc.soloBand = ctx.proc.soloBand.load() == band ? -1 : band;
        else if (r == 3)
        {
            ctx.proc.removeBands ({ band });
            ctx.selection.clear();
        }
    });
}
} // namespace lens::gui
