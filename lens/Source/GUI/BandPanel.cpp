#include "BandPanel.h"

#include "Coach/CoachContent.h"
#include "Params/Parameters.h"
#include "Util/Format.h"

namespace lens::gui
{
namespace
{
namespace pb = params::band;

void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    s.setTitle (name);
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    l.setText (name, juce::dontSendNotification);
    l.setFont (theme::font (11.0f, true));
    l.setColour (juce::Label::textColourId, theme::textDim);
    l.setJustificationType (juce::Justification::centred);
    l.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (s);
    parent.addAndMakeVisible (l);
}

juce::String tip (const char* control) { return coach::Content::get().tooltip (control); }
} // namespace

BandPanel::BandPanel (EditorContext& c) : ctx (c)
{
    title.setFont (theme::font (14.0f, true));
    addAndMakeVisible (title);
    hint.setFont (theme::font (13.0f));
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hint);

    for (auto* b : { &onBtn, &dynBtn, &soloBtn, &customBtn, &listenBtn })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    shapeBox.addItemList ({ "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt", "All Pass" }, 1);
    slopeBox.addItemList ({ "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" }, 1);
    stereoBox.addItemList ({ "Stereo", "Mid", "Side", "Left", "Right" }, 1);
    modeBox.addItemList ({ "Above", "Below" }, 1);
    sourceBox.addItemList ({ "Internal", "External" }, 1);
    for (auto* b : { &shapeBox, &slopeBox, &stereoBox, &modeBox, &sourceBox })
        addAndMakeVisible (*b);

    setupKnob (freq, freqL, "Frequency", *this);
    setupKnob (gain, gainL, "Gain", *this);
    setupKnob (q, qL, "Q", *this);
    setupKnob (range, rangeL, "Range", *this);
    setupKnob (thresh, threshL, "Threshold", *this);
    setupKnob (ratio, ratioL, "Ratio", *this);
    setupKnob (attack, attackL, "Attack", *this);
    setupKnob (release, releaseL, "Release", *this);
    setupKnob (hp, hpL, "Det HP", *this);
    setupKnob (lp, lpL, "Det LP", *this);
    addAndMakeVisible (learnBtn);
    dynTitle.setText ("Dynamics", juce::dontSendNotification);
    dynTitle.setFont (theme::font (13.0f, true));
    addAndMakeVisible (dynTitle);
    activity.setFont (theme::font (12.0f));
    activity.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (activity);

    // Tooltips and accessibility names from LESSONS.md (Controls table).
    freq.setTooltip (tip ("Frequency"));
    gain.setTooltip (tip ("Gain"));
    q.setTooltip (tip ("Q"));
    shapeBox.setTooltip (tip ("Shape"));
    slopeBox.setTooltip (tip ("Slope"));
    stereoBox.setTooltip (tip ("Stereo mode"));
    soloBtn.setTooltip (tip ("Solo"));
    dynBtn.setTooltip (tip ("Make dynamic"));
    modeBox.setTooltip (tip ("Mode"));
    range.setTooltip (tip ("Range"));
    thresh.setTooltip (tip ("Threshold"));
    learnBtn.setTooltip (tip ("Learn"));
    ratio.setTooltip (tip ("Ratio"));
    attack.setTooltip (tip ("Attack"));
    release.setTooltip (tip ("Release"));
    sourceBox.setTooltip (tip ("Detector source"));
    customBtn.setTooltip (tip ("Custom detector"));
    hp.setTooltip (tip ("Custom detector"));
    lp.setTooltip (tip ("Custom detector"));
    listenBtn.setTooltip (tip ("Listen"));
    activity.setTooltip (tip ("Activity"));
    shapeBox.setTitle ("Shape");
    slopeBox.setTitle ("Slope");
    stereoBox.setTitle ("Stereo mode");
    modeBox.setTitle ("Mode");
    sourceBox.setTitle ("Detector source");
    onBtn.setTitle ("Band on");
    dynBtn.setTitle ("Make dynamic");
    soloBtn.setTitle ("Solo");
    customBtn.setTitle ("Custom detector");
    listenBtn.setTitle ("Listen");
    learnBtn.setTitle ("Learn");
    activity.setTitle ("Activity");

    soloBtn.onClick = [this]
    {
        ctx.proc.soloBand = soloBtn.getToggleState() ? band : -1;
    };
    listenBtn.onClick = [this]
    {
        ctx.proc.listenBand = listenBtn.getToggleState() ? band : -1;
        ctx.uiEvent (listenBtn.getToggleState() ? "listen on" : "listen off");
    };
    learnBtn.onClick = [this]
    {
        if (band >= 0)
            ctx.proc.toggleLearn (band);
    };
    dynBtn.onStateChange = [] {};

    ctx.selection.addChangeListener (this);
    bind();
}

BandPanel::~BandPanel()
{
    ctx.selection.removeChangeListener (this);
}

void BandPanel::bind()
{
    sliderAtt.clear();
    comboAtt.clear();
    buttonAtt.clear();
    band = ctx.selection.primary();

    const bool has = band >= 0;
    for (auto* c : std::initializer_list<juce::Component*> { &onBtn, &dynBtn, &soloBtn, &shapeBox, &slopeBox, &stereoBox, &freq, &gain, &q, &freqL, &gainL, &qL, &title })
        c->setVisible (has);
    hint.setVisible (! has);
    hint.setText ("Double-click the display to add a band.", juce::dontSendNotification);
    if (! has)
    {
        dynamicShown = false;
        for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &sourceBox, &range, &thresh, &ratio, &attack, &release, &hp, &lp, &rangeL, &threshL, &ratioL,
                                                                 &attackL, &releaseL, &hpL, &lpL, &activity, &dynTitle, &learnBtn, &customBtn, &listenBtn })
            c->setVisible (false);
        repaint();
        return;
    }

    auto& apvts = ctx.proc.getApvts();
    auto id = [this] (const char* s) { return params::bandParamId (band + 1, s); };
    auto attachS = [&] (juce::Slider& s, const char* suffix)
    {
        sliderAtt.push_back (std::make_unique<SA> (apvts, id (suffix), s));
        auto* p = apvts.getParameter (id (suffix));
        s.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    };
    attachS (freq, pb::freq);
    attachS (gain, pb::gain);
    attachS (q, pb::q);
    attachS (range, pb::dynRange);
    attachS (thresh, pb::dynThresh);
    attachS (ratio, pb::dynRatio);
    attachS (attack, pb::dynAttack);
    attachS (release, pb::dynRelease);
    attachS (hp, pb::detHp);
    attachS (lp, pb::detLp);
    comboAtt.push_back (std::make_unique<CA> (apvts, id (pb::shape), shapeBox));
    comboAtt.push_back (std::make_unique<CA> (apvts, id (pb::slope), slopeBox));
    comboAtt.push_back (std::make_unique<CA> (apvts, id (pb::stereo), stereoBox));
    comboAtt.push_back (std::make_unique<CA> (apvts, id (pb::dynMode), modeBox));
    comboAtt.push_back (std::make_unique<CA> (apvts, id (pb::detSource), sourceBox));
    buttonAtt.push_back (std::make_unique<BA> (apvts, id (pb::on), onBtn));
    buttonAtt.push_back (std::make_unique<BA> (apvts, id (pb::dynOn), dynBtn));
    buttonAtt.push_back (std::make_unique<BA> (apvts, id (pb::detCustom), customBtn));

    // Readable values: "2.5 kHz A6", "+3.0 dB", "10.0 ms".
    freq.textFromValueFunction = [] (double v) { return fmt::freq (v) + "  " + fmt::note (v).name; };
    freq.valueFromTextFunction = [] (const juce::String& t)
    {
        const double v = t.getDoubleValue();
        return t.containsIgnoreCase ("k") ? v * 1000.0 : v;
    };
    hp.textFromValueFunction = lp.textFromValueFunction = [] (double v) { return fmt::freq (v); };
    hp.valueFromTextFunction = lp.valueFromTextFunction = freq.valueFromTextFunction;
    gain.textFromValueFunction = range.textFromValueFunction = [] (double v) { return fmt::gain (v); };
    thresh.textFromValueFunction = [] (double v) { return fmt::number (v, 1) + " dB"; };
    q.textFromValueFunction = [] (double v) { return juce::String (v, v < 10.0 ? 2 : 1); };
    ratio.textFromValueFunction = [] (double v) { return juce::String (v, 1) + ":1"; };
    attack.textFromValueFunction = release.textFromValueFunction = [] (double v)
    { return v < 10.0 ? juce::String (v, 1) + " ms" : (v < 1000.0 ? juce::String ((int) std::lround (v)) + " ms" : juce::String (v / 1000.0, 2) + " s"); };
    for (auto* s : { &freq, &gain, &q, &range, &thresh, &ratio, &attack, &release, &hp, &lp })
    {
        s->updateText();
        s->onDragEnd = [this] { ctx.uiEvent ("knob changed"); };
    }
    for (auto* s : { &freq, &gain })
        s->onValueChange = [this] { ctx.uiEvent ("band moved"); };
    q.onValueChange = [this] { ctx.uiEvent ("q changed"); };

    title.setText ("Band " + juce::String (band + 1).paddedLeft ('0', 2), juce::dontSendNotification);
    soloBtn.setToggleState (ctx.proc.soloBand.load() == band, juce::dontSendNotification);
    listenBtn.setToggleState (ctx.proc.listenBand.load() == band, juce::dontSendNotification);
    refresh();
    resized();
    repaint();
}

void BandPanel::refresh()
{
    if (band < 0)
        return;
    const auto shape = (dsp::Shape) (int) ctx.proc.bandValue (band, pb::shape);
    const bool dynCapable = dsp::canBeDynamic (shape);
    const bool dyn = dynCapable && ctx.proc.bandValue (band, pb::dynOn) > 0.5f;

    gain.setEnabled (dsp::usesGain (shape));
    q.setEnabled (! dsp::isCut (shape) && shape != dsp::Shape::Tilt);
    slopeBox.setEnabled (dsp::isCut (shape));
    stereoBox.setEnabled (! ctx.proc.isMono());
    dynBtn.setEnabled (dynCapable);

    if (dyn != dynamicShown)
    {
        dynamicShown = dyn;
        for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &sourceBox, &range, &thresh, &ratio, &attack, &release, &hp, &lp, &rangeL, &threshL, &ratioL,
                                                                 &attackL, &releaseL, &hpL, &lpL, &activity, &dynTitle, &learnBtn, &customBtn, &listenBtn })
            c->setVisible (dyn);
        resized();
        repaint();
    }
    if (! dyn)
        return;

    const bool custom = ctx.proc.bandValue (band, pb::detCustom) > 0.5f;
    hp.setEnabled (custom);
    lp.setEnabled (custom);

    const float act = ctx.proc.stats().bands[(size_t) band].activity.load();
    activity.setText ("Activity " + juce::String ((int) std::lround (act)) + "%", juce::dontSendNotification);

    if (ctx.proc.isLearning (band))
    {
        const float pr = ctx.proc.learnProgress (band);
        learnBtn.setButtonText (pr > 0.0f ? "Learning " + juce::String ((int) (pr * 100)) + "%" : "Learning");
        learnBtn.setToggleState (true, juce::dontSendNotification);
    }
    else
    {
        learnBtn.setButtonText ("Learn");
        learnBtn.setToggleState (false, juce::dontSendNotification);
    }
    soloBtn.setToggleState (ctx.proc.soloBand.load() == band, juce::dontSendNotification);
    listenBtn.setToggleState (ctx.proc.listenBand.load() == band, juce::dontSendNotification);
}

void BandPanel::updateHighlights()
{
    const auto& h = ctx.highlights;
    auto mark = [] (juce::Component& c, bool on)
    {
        if ((bool) c.getProperties().getWithDefault ("highlight", false) != on)
        {
            c.getProperties().set ("highlight", on);
            c.repaint();
        }
    };
    mark (dynBtn, h.contains ("dynamic"));
    mark (learnBtn, h.contains ("learn"));
    mark (customBtn, h.contains ("custom detector"));
    mark (listenBtn, h.contains ("listen"));
    mark (sourceBox, h.contains ("detector source"));
    mark (shapeBox, h.contains ("shape"));
    for (auto* s : std::initializer_list<juce::Component*> { &attack, &release })
        mark (*s, h.contains ("attack/release"));
    mark (gain, h.contains ("gain"));
    mark (q, h.contains ("q"));
    mark (thresh, h.contains ("learn"));
    repaint();
}

void BandPanel::layoutKnob (juce::Slider& s, juce::Label& l, juce::Rectangle<int> r)
{
    l.setBounds (r.removeFromTop (14));
    s.setBounds (r);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    hint.setBounds (r);
    const int bandW = dynamicShown ? 430 : r.getWidth();
    bandArea = r.removeFromLeft (bandW);
    dynArea = r.withTrimmedLeft (10);

    auto b = bandArea;
    auto top = b.removeFromTop (24);
    title.setBounds (top.removeFromLeft (80));
    onBtn.setBounds (top.removeFromLeft (44));
    top.removeFromLeft (6);
    shapeBox.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (6);
    slopeBox.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (6);
    stereoBox.setBounds (top.removeFromLeft (80));

    b.removeFromTop (6);
    auto right = b.removeFromRight (82);
    dynBtn.setBounds (right.removeFromTop (26).reduced (0, 1));
    right.removeFromTop (6);
    soloBtn.setBounds (right.removeFromTop (26).reduced (0, 1));
    const int kw = juce::jmin (100, b.getWidth() / 3);
    layoutKnob (freq, freqL, b.removeFromLeft (kw));
    layoutKnob (gain, gainL, b.removeFromLeft (kw));
    layoutKnob (q, qL, b.removeFromLeft (kw));

    if (! dynamicShown)
        return;
    auto d = dynArea;
    auto dt = d.removeFromTop (24);
    dynTitle.setBounds (dt.removeFromLeft (80));
    modeBox.setBounds (dt.removeFromLeft (80));
    dt.removeFromLeft (6);
    sourceBox.setBounds (dt.removeFromLeft (90));
    dt.removeFromLeft (6);
    customBtn.setBounds (dt.removeFromLeft (66));
    dt.removeFromLeft (6);
    listenBtn.setBounds (dt.removeFromLeft (60));
    dt.removeFromLeft (8);
    activity.setBounds (dt);

    d.removeFromTop (6);
    auto learnCol = d.removeFromRight (84);
    learnBtn.setBounds (learnCol.removeFromTop (28));
    const int dk = d.getWidth() / 7;
    layoutKnob (range, rangeL, d.removeFromLeft (dk));
    layoutKnob (thresh, threshL, d.removeFromLeft (dk));
    layoutKnob (ratio, ratioL, d.removeFromLeft (dk));
    layoutKnob (attack, attackL, d.removeFromLeft (dk));
    layoutKnob (release, releaseL, d.removeFromLeft (dk));
    layoutKnob (hp, hpL, d.removeFromLeft (dk));
    layoutKnob (lp, lpL, d.removeFromLeft (dk));
}

void BandPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (dynamicShown)
    {
        g.setColour (theme::outline);
        g.drawVerticalLine (dynArea.getX() - 5, (float) dynArea.getY(), (float) dynArea.getBottom());
    }
    // Outline highlighted knobs.
    for (auto* c : getChildren())
        if (c->isVisible() && dynamic_cast<juce::Slider*> (c) != nullptr && (bool) c->getProperties().getWithDefault ("highlight", false))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (c->getBounds().toFloat().expanded (2.0f, 16.0f).withTrimmedBottom (14.0f), 5.0f, 1.5f);
        }
}
} // namespace lens::gui
