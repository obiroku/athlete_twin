#include "MainView.h"

#include "Coach/CoachContent.h"
#include "Params/Parameters.h"
#include "Util/Format.h"

namespace lens::gui
{
namespace
{
juce::String tip (const char* control) { return coach::Content::get().tooltip (control); }
const char* stepNames[] = { "CLEAN", "CONTROL", "SHAPE", "CHECK" };
} // namespace

MainView::MainView (EditorContext& c) : ctx (c), spectrum (c), bandPanel (c)
{
    setSize (logicalWidth, logicalHeight);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (bandPanel);

    auto& apvts = ctx.proc.getApvts();

    // ---- Presets.
    for (auto* b : { &prevPreset, &nextPreset, &presetName })
        addAndMakeVisible (*b);
    prevPreset.setTitle ("Previous preset");
    nextPreset.setTitle ("Next preset");
    presetName.setTitle ("Preset browser");
    prevPreset.onClick = [this] { if (onPrevPreset) onPrevPreset(); };
    nextPreset.onClick = [this] { if (onNextPreset) onNextPreset(); };
    presetName.onClick = [this] { if (onPresetNameClicked) onPresetNameClicked(); };
    presetNote.setFont (theme::font (12.0f));
    presetNote.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (presetNote);

    // ---- Track type (state, not a parameter; never changes the sound).
    trackType.addItemList (state::SessionState::trackTypes(), 1);
    trackType.setTooltip (tip ("Track type"));
    trackType.setTitle ("Track type");
    trackType.onChange = [this]
    {
        auto s = ctx.proc.getSession();
        s.trackType = trackType.getText();
        ctx.proc.setSession (s);
        ctx.uiEvent ("track type");
        spectrum.repaint();
    };
    addAndMakeVisible (trackType);

    // ---- Workflow steps.
    for (int i = 0; i < 4; ++i)
    {
        steps[i].setButtonText (stepNames[i]);
        steps[i].setClickingTogglesState (false);
        steps[i].setTooltip (tip ("Workflow steps"));
        steps[i].setTitle (juce::String (stepNames[i]) + " step");
        steps[i].onClick = [this, i] { setStep (stepNames[i]); };
        addAndMakeVisible (steps[i]);
    }

    // ---- Phase mode, resolution, latency.
    phaseMode.addItemList ({ "Zero Latency", "Linear Phase" }, 1);
    resolution.addItemList ({ "Low", "Medium", "High" }, 1);
    phaseMode.setTooltip (tip ("Phase mode"));
    resolution.setTooltip (tip ("Resolution"));
    phaseMode.setTitle ("Phase mode");
    resolution.setTitle ("Resolution");
    phaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::id::phaseMode, phaseMode);
    resAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::id::lpResolution, resolution);
    addAndMakeVisible (phaseMode);
    addAndMakeVisible (resolution);
    for (auto* l : { &latency, &cpu })
    {
        l->setFont (theme::font (11.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*l);
    }
    latency.setTitle ("Latency");
    cpu.setTitle ("CPU");

    // ---- A/B, undo, Coach.
    for (auto* b : { &abA, &abB, &abCopy, &undoBtn, &redoBtn, &coachBtn })
        addAndMakeVisible (*b);
    abA.setTooltip (tip ("A/B"));
    abB.setTooltip (tip ("A/B"));
    abCopy.setTooltip (tip ("A/B"));
    abA.setTitle ("A setup");
    abB.setTitle ("B setup");
    abCopy.setTitle ("Copy A/B");
    coachBtn.setTooltip (tip ("Coach"));
    coachBtn.setTitle ("Coach");
    auto setSlot = [this] (int slot)
    {
        auto* p = ctx.proc.getApvts().getParameter (params::id::abSlot);
        p->beginChangeGesture();
        p->setValueNotifyingHost ((float) slot);
        p->endChangeGesture();
        ctx.uiEvent ("ab switched");
    };
    abA.onClick = [setSlot] { setSlot (0); };
    abB.onClick = [setSlot] { setSlot (1); };
    abCopy.onClick = [this]
    {
        const bool b = ctx.proc.getApvts().getRawParameterValue (params::id::abSlot)->load() > 0.5f;
        juce::PopupMenu m;
        m.addItem (1, b ? "Copy B to A" : "Copy A to B");
        m.addItem (2, b ? "Copy A to B" : "Copy B to A");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&abCopy), [this] (int r)
        {
            if (r == 1)
                ctx.proc.copyActiveToOther();
            else if (r == 2)
                ctx.proc.copyOtherToActive();
        });
    };
    undoBtn.onClick = [this] { ctx.proc.undo(); };
    redoBtn.onClick = [this] { ctx.proc.redo(); };
    coachBtn.setClickingTogglesState (true);
    coachBtn.onClick = [this]
    {
        setCoachVisible (coachBtn.getToggleState());
        auto& st = ctx.proc.coachSettings();
        st.coachVisible = coachBtn.getToggleState();
        st.save();
    };

    // ---- Output section.
    outGain.setSliderStyle (juce::Slider::LinearBar);
    outGain.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 110, 22);
    outGain.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.5f));
    outGain.setColour (juce::Slider::backgroundColourId, theme::panelRaised);
    outGain.setTooltip (tip ("Output gain"));
    outGain.setTitle ("Output gain");
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::id::outputGain, outGain);
    outGain.textFromValueFunction = [] (double v) { return "Out " + fmt::gain (v); };
    outGain.setDoubleClickReturnValue (true, 0.0);
    outGain.updateText();
    addAndMakeVisible (outGain);

    autoGain.setTooltip (tip ("Auto-gain"));
    delta.setTooltip (tip ("Delta"));
    bypass.setTooltip (tip ("Bypass"));
    autoGain.setTitle ("Auto-gain");
    delta.setTitle ("Delta");
    bypass.setTitle ("Bypass");
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::id::autoGain, autoGain);
    deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::id::delta, delta);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::id::bypass, bypass);
    autoGain.onClick = [this] { ctx.uiEvent ("auto-gain"); };
    delta.onClick = [this] { ctx.uiEvent (delta.getToggleState() ? "delta on" : "delta off"); };
    bypass.onClick = [this] { ctx.uiEvent ("bypass toggled"); };
    for (auto* b : { &autoGain, &delta, &bypass })
        addAndMakeVisible (*b);

    loudness.setFont (theme::font (11.5f));
    loudness.setJustificationType (juce::Justification::centredRight);
    loudness.setTooltip (tip ("Loudness"));
    loudness.setTitle ("Loudness");
    addAndMakeVisible (loudness);

    const auto s = ctx.proc.getSession();
    trackType.setText (s.trackType, juce::dontSendNotification);
    setStep (s.workflowStep);
    refreshPresetLabel();
    updateTopBar();
}

MainView::~MainView() = default;

void MainView::refreshPresetLabel()
{
    const auto s = ctx.proc.getSession();
    presetName.setButtonText (s.presetName);
    presetNote.setText (s.note, juce::dontSendNotification);
    trackType.setText (s.trackType, juce::dontSendNotification);
}

void MainView::setStep (const juce::String& step)
{
    auto s = ctx.proc.getSession();
    s.workflowStep = step;
    ctx.proc.setSession (s);
    for (int i = 0; i < 4; ++i)
        steps[i].setToggleState (step == stepNames[i], juce::dontSendNotification);

    juce::StringArray h;
    if (step == "CLEAN")
        h = { "peak markers", "shape" };
    else if (step == "CONTROL")
        h = { "dynamic", "learn" };
    else if (step == "SHAPE")
        h = { "gain", "q" };
    else if (step == "CHECK")
        h = { "auto-gain", "bypass", "delta", "a/b", "loudness" };
    setHighlights (h);
    ctx.uiEvent ("step " + step);
}

void MainView::setHighlights (const juce::StringArray& h)
{
    ctx.highlights = h;
    markHighlights();
    if (ctx.onHighlightsChanged)
        ctx.onHighlightsChanged();
}

void MainView::markHighlights()
{
    const auto& h = ctx.highlights;
    auto mark = [] (juce::Component& c, bool on)
    {
        c.getProperties().set ("highlight", on);
        c.repaint();
    };
    mark (autoGain, h.contains ("auto-gain"));
    mark (bypass, h.contains ("bypass"));
    mark (delta, h.contains ("delta"));
    mark (abA, h.contains ("a/b"));
    mark (abB, h.contains ("a/b"));
    mark (trackType, h.contains ("track type"));
    bandPanel.updateHighlights();
    spectrum.repaint();
    repaint();
}

void MainView::setCoachVisible (bool v)
{
    coachVisible = v;
    coachBtn.setToggleState (v, juce::dontSendNotification);
    if (coachComponent != nullptr)
        coachComponent->setVisible (v);
    resized();
}

void MainView::updateTopBar()
{
    const bool lp = ctx.proc.getApvts().getRawParameterValue (params::id::phaseMode)->load() > 0.5f;
    resolution.setVisible (lp);
    const double fs = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    const int lat = ctx.proc.stats().latencySamples.load();
    latency.setText (lat > 0 ? juce::String (1000.0 * lat / fs, 1) + " ms" : "0 ms", juce::dontSendNotification);
    latency.setVisible (lp || lat > 0);
    cpu.setText ("CPU " + juce::String (ctx.proc.cpuLoad() * 100.0f, 1) + "%", juce::dontSendNotification);

    const bool b = ctx.proc.getApvts().getRawParameterValue (params::id::abSlot)->load() > 0.5f;
    abA.setToggleState (! b, juce::dontSendNotification);
    abB.setToggleState (b, juce::dontSendNotification);
    undoBtn.setEnabled (ctx.proc.history().canUndo());
    redoBtn.setEnabled (ctx.proc.history().canRedo());

    auto& st = ctx.proc.stats();
    const float in = st.inputLufs.load(), out = st.outputLufs.load();
    auto lu = [] (float v) { return v > -150.0f ? fmt::number (v, 1) : juce::String ("--"); };
    juce::String text = "In " + lu (in) + "  Out " + lu (out);
    if (in > -150.0f && out > -150.0f)
    {
        const float d = out - in;
        text << "  " << (d >= 0 ? "+" : fmt::minus) << juce::String (std::abs (d), 1) << " LU";
    }
    loudness.setText (text, juce::dontSendNotification);
    outGain.updateText();
}

void MainView::refresh (double nowMs)
{
    spectrum.refresh();
    if (nowMs - lastSlowRefresh > 100.0)
    {
        lastSlowRefresh = nowMs;
        bandPanel.refresh();
        updateTopBar();
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::panel);
    g.fillRect (topBar);
    if (ctx.highlights.contains ("loudness"))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (loudness.getBounds().toFloat().expanded (2.0f), 4.0f, 1.5f);
    }
}

void MainView::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (66);

    auto bar = topBar.reduced (8, 5);
    auto row1 = bar.removeFromTop (27);
    bar.removeFromTop (5);
    auto row2 = bar;

    // Right block: output section.
    auto right1 = row1.removeFromRight (236);
    auto right2 = row2.removeFromRight (236);
    outGain.setBounds (right1.removeFromLeft (110).reduced (0, 2));
    right1.removeFromLeft (4);
    loudness.setBounds (right1);
    autoGain.setBounds (right2.removeFromLeft (64).reduced (0, 1));
    right2.removeFromLeft (4);
    delta.setBounds (right2.removeFromLeft (64).reduced (0, 1));
    right2.removeFromLeft (4);
    bypass.setBounds (right2.reduced (0, 1));

    prevPreset.setBounds (row1.removeFromLeft (24));
    presetName.setBounds (row1.removeFromLeft (150).reduced (2, 0));
    nextPreset.setBounds (row1.removeFromLeft (24));
    row1.removeFromLeft (8);
    trackType.setBounds (row1.removeFromLeft (128));
    row1.removeFromLeft (8);
    for (auto& s : steps)
    {
        s.setBounds (row1.removeFromLeft (64).reduced (1, 0));
    }
    row1.removeFromLeft (8);
    phaseMode.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (4);
    resolution.setBounds (row1.removeFromLeft (76));

    presetNote.setBounds (row2.removeFromLeft (300));
    abA.setBounds (row2.removeFromLeft (30).reduced (1, 0));
    abB.setBounds (row2.removeFromLeft (30).reduced (1, 0));
    abCopy.setBounds (row2.removeFromLeft (48).reduced (1, 0));
    row2.removeFromLeft (8);
    undoBtn.setBounds (row2.removeFromLeft (48).reduced (1, 0));
    redoBtn.setBounds (row2.removeFromLeft (48).reduced (1, 0));
    row2.removeFromLeft (8);
    coachBtn.setBounds (row2.removeFromLeft (58).reduced (1, 0));
    row2.removeFromLeft (6);
    latency.setBounds (row2.removeFromLeft (64));
    cpu.setBounds (row2);

    bandPanel.setBounds (r.removeFromBottom (150));
    if (coachVisible && coachComponent != nullptr)
        coachComponent->setBounds (r.removeFromRight (280));
    spectrum.setBounds (r);
    if (overlayComponent != nullptr)
        overlayComponent->setBounds (getLocalBounds().withTrimmedTop (66));
    if (matchComponent != nullptr)
        matchComponent->setBounds (getLocalBounds().withTrimmedTop (66));
}
} // namespace lens::gui
