#include "Editor.h"

#include "CoachPanel.h"
#include "PresetBrowser.h"

namespace lens::gui
{
LensEditor::LensEditor (LensProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      analyzerThread (p.getEngine().preFifo, p.getEngine().postFifo, p.getEngine().sidechainFifo),
      ctx { p, analyzerThread, {}, {}, {}, {}, {} },
      vblank (this, [this] { onVBlank(); })
{
    setLookAndFeel (&lnf);
    main = std::make_unique<MainView> (ctx);
    addAndMakeVisible (*main);

    auto coachPanel = std::make_unique<CoachPanel> (ctx, *main);
    const bool startVisible = coachPanel->shouldStartVisible();
    coach = std::move (coachPanel);
    main->addChildComponent (*coach);
    main->coachComponent = coach.get();
    main->setCoachVisible (startVisible);
    browser = std::make_unique<PresetBrowser> (ctx, *main);
    browser->setVisible (false);
    main->addChildComponent (*browser);
    main->overlayComponent = browser.get();
    main->resized();

    analyzerThread.start (p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0);

    const double scale = juce::jlimit (0.75, 2.0, p.getSession().guiScale);
    setResizable (true, true);
    setResizeLimits ((int) (MainView::logicalWidth * 0.75), (int) (MainView::logicalHeight * 0.75), MainView::logicalWidth * 2, MainView::logicalHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainView::logicalWidth / MainView::logicalHeight);
    setSize ((int) std::lround (MainView::logicalWidth * scale), (int) std::lround (MainView::logicalHeight * scale));
}

LensEditor::~LensEditor()
{
    analyzerThread.stop();
    // UI-only state never outlives the editor.
    proc.soloBand = -1;
    proc.listenBand = -1;
    proc.earTrainer.on = false;
    browser.reset();
    coach.reset();
    main.reset();
    setLookAndFeel (nullptr);
}

void LensEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void LensEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::logicalWidth;
    main->setTransform (juce::AffineTransform::scale (scale));
    main->setBounds (0, 0, MainView::logicalWidth, MainView::logicalHeight);
    auto s = proc.getSession();
    s.guiScale = juce::jlimit (0.75, 2.0, (double) scale);
    proc.setSession (s);
}

void LensEditor::onVBlank()
{
    // At most 60 frames per second.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastFrameMs < 1000.0 / 60.0 - 1.0)
        return;
    lastFrameMs = now;
    main->refresh (now);
}
} // namespace lens::gui
