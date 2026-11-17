#include "EarTrainerView.h"

#include "SpectrumDisplay.h"

namespace lens::gui
{
EarTrainerView::EarTrainerView (EditorContext& c, SpectrumDisplay& d) : ctx (c), display (d), game (c.proc.coachSettings())
{
    hidden.setClickingTogglesState (true);
    hidden.setTitle ("Hidden band");
    hidden.onClick = [this] { applyBand(); };
    addAndMakeVisible (hidden);
}

EarTrainerView::~EarTrainerView()
{
    // Turns off (with the band's own 10 ms fade) when the editor closes.
    ctx.proc.earTrainer.on = false;
    display.earTrainerActive = false;
    display.onEarGuess = nullptr;
}

void EarTrainerView::setActive (bool a)
{
    if (a)
    {
        game.start();
        message = game.instructions();
        display.earTrainerActive = true;
        display.onEarGuess = [this] (double f) { onGuess (f); };
    }
    else
    {
        game.stop();
        display.earTrainerActive = false;
        display.onEarGuess = nullptr;
        hidden.setToggleState (false, juce::dontSendNotification);
    }
    applyBand();
    display.repaint();
    repaint();
}

void EarTrainerView::applyBand()
{
    auto& et = ctx.proc.earTrainer;
    const auto& lv = game.level();
    et.freq = (float) game.targetFreq();
    et.gainDb = lv.gainDb;
    et.q = lv.q;
    et.on = game.isActive() && hidden.getToggleState();
}

void EarTrainerView::onGuess (double freq)
{
    const auto r = game.guess (freq);
    message = r.message;
    hidden.setToggleState (false, juce::dontSendNotification);
    applyBand();
    repaint();
}

void EarTrainerView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (6);
    g.setColour (theme::accent);
    g.setFont (theme::font (18.0f, true));
    g.drawText ("EAR TRAINER ON", r.removeFromTop (26), juce::Justification::centredLeft);
    r.removeFromTop (4);

    auto& s = ctx.proc.coachSettings();
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("Level " + juce::String (s.earLevel) + "   " + juce::String (game.question() + 1) + "/10   " + juce::String (game.rightThisRound()) + " right",
                r.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (theme::font (12.0f));
    g.setColour (theme::textDim);
    g.drawText ("Streak " + juce::String (s.earStreak) + "   best " + juce::String (s.earBestStreak), r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (40);

    juce::AttributedString text;
    text.append (message, theme::font (13.0f), theme::text);
    text.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (text, (float) r.getWidth());
    tl.draw (g, r.toFloat());
}

void EarTrainerView::resized()
{
    hidden.setBounds (getLocalBounds().reduced (6).withTrimmedTop (74).withHeight (30).withWidth (140));
}
} // namespace lens::gui
