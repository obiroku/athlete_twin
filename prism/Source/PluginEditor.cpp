#include "PluginEditor.h"

namespace prism
{
PrismEditor::PrismEditor (PrismProcessor& p)
    : AudioProcessorEditor (p), proc (p), browser (p), topBar (p, browser),
      vblank (this, [this] (double t) { onVBlank (t); })
{
    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (tabs);
    content.addChildComponent (browser);
    browser.onClose = [this] { browser.setVisible (false); };

    tabs.setTabBarDepth (28);
    auto add = [this] (const juce::String& name, gui::Page* pg) {
        pages.push_back (pg);
        tabs.addTab (name, gui::col::bg, pg, true);
    };
    add ("SYNTH", new gui::SynthPage (p));
    add ("MOD", new gui::ModMatrixPage (p));
    add ("FX", new gui::FxPage (p));
    add ("WAVETABLE", new gui::WavetablePage (p));
    add ("ARP / VOICING", new gui::ArpVoicingPage (p));
    add ("SETTINGS", new gui::SettingsPage (p, [this] (double s) { setScale (s); }));

    if (p.library.settings.tooltips) tooltip = std::make_unique<juce::TooltipWindow> (this, 700);
    setTitle ("PRISM");
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) BaseW / BaseH);
        c->setSizeLimits ((int) (BaseW * 0.75), (int) (BaseH * 0.75), BaseW * 2, BaseH * 2);
    }
    const double s = juce::jlimit (0.75, 2.0, p.guiScale);
    setSize ((int) std::lround (BaseW * s), (int) std::lround (BaseH * s));
}

PrismEditor::~PrismEditor()
{
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PrismEditor::setScale (double s)
{
    s = juce::jlimit (0.75, 2.0, s);
    setSize ((int) std::lround (BaseW * s), (int) std::lround (BaseH * s));
}

std::atomic<int> gEditorPaints { 0 };
void PrismEditor::paint (juce::Graphics& g)
{
    ++gEditorPaints;
    g.fillAll (gui::col::bg);
}

void PrismEditor::resized()
{
    const double s = (double) getWidth() / BaseW;
    content.setBounds (0, 0, BaseW, BaseH);
    content.setTransform (juce::AffineTransform::scale ((float) s));
    topBar.setBounds (0, 0, BaseW, 44);
    tabs.setBounds (0, 44, BaseW, BaseH - 44);
    browser.setBounds (100, 44, 520, 420);
    proc.guiScale = s;
    proc.library.settings.guiScale = s;
}

void PrismEditor::mouseDown (const juce::MouseEvent&)
{
    browser.setVisible (false);
}

void PrismEditor::onVBlank (double t)
{
    // at most 60 fps; only components that changed repaint (dirty regions)
    if (t - lastFrame < 1.0 / 61.0) return;
    lastFrame = t;
    if (tabs.getCurrentTabIndex() != liveTab)
    {
        liveTab = tabs.getCurrentTabIndex();
        live.clear();
        if (liveTab >= 0 && liveTab < (int) pages.size()) pages[(size_t) liveTab]->collectLive (live);
    }
    topBar.refreshLive();
    for (auto* l : live) l->refreshLive();
}
} // namespace prism
