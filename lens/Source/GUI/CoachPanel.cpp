#include "CoachPanel.h"

#include "Coach/CoachState.h"
#include "Coach/Explain.h"
#include "MainView.h"
#include "Params/Parameters.h"

namespace lens::gui
{
namespace
{
// Glossary terms and the words that link to them ("Low cut / High cut"
// links from "low cut" and "high cut").
const std::vector<std::pair<juce::String, juce::String>>& linkWords()
{
    static const auto words = []
    {
        std::vector<std::pair<juce::String, juce::String>> w;
        for (const auto& g : coach::Content::get().glossary)
        {
            auto t = g.term;
            if (t.contains ("("))
            {
                // "Decibel (dB)": link the word, not every "dB".
                w.push_back ({ t.upToFirstOccurrenceOf ("(", false, false).trim(), g.term });
            }
            else if (t.contains ("/"))
            {
                // "Low cut / High cut": only multi-word parts, so a lone
                // "Side" or "Mid" never links.
                for (auto part : juce::StringArray::fromTokens (t, "/", ""))
                    if (part.trim().containsChar (' '))
                        w.push_back ({ part.trim(), g.term });
            }
            else
            {
                w.push_back ({ t, g.term });
                if (t.containsChar ('-'))
                    w.push_back ({ t.replaceCharacter ('-', ' '), g.term }); // "Side Chain"
            }
        }
        // Longer phrases first so "Spectrum analyzer" wins over shorter words.
        std::sort (w.begin(), w.end(), [] (const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
        return w;
    }();
    return words;
}

bool isWordChar (juce::juce_wchar c) { return juce::CharacterFunctions::isLetterOrDigit (c) || c == '-'; }

const char* tabNames[] = { "Now", "Recipes", "Lessons", "Glossary" };
} // namespace

// ---------------------------------------------------------------- LinkText

LinkText::LinkText (const juce::String& t, float fontSize, juce::Colour c, std::function<void (const juce::String&)> cb)
    : text (t), size (fontSize), colour (c), onTerm (std::move (cb))
{
    // Find each glossary word once (whole words, case-insensitive).
    std::vector<bool> used ((size_t) text.length(), false);
    juce::StringArray linked;
    const auto lower = text.toLowerCase();
    for (const auto& [word, term] : linkWords())
    {
        if (word.length() < 2 || linked.contains (term))
            continue;
        const auto w = word.toLowerCase();
        int from = 0;
        while ((from = lower.indexOf (from, w)) >= 0)
        {
            const int end = from + w.length();
            const bool startOk = from == 0 || ! isWordChar (text[from - 1]);
            const bool endOk = end >= text.length() || ! isWordChar (text[end]) || (text[end] == 's' && (end + 1 >= text.length() || ! isWordChar (text[end + 1])));
            bool free = true;
            for (int i = from; i < end; ++i)
                free = free && ! used[(size_t) i];
            if (startOk && endOk && free)
            {
                for (int i = from; i < end; ++i)
                    used[(size_t) i] = true;
                links.push_back ({ { from, end }, term });
                linked.add (term);
                break;
            }
            from = end;
        }
    }
    std::sort (links.begin(), links.end(), [] (const auto& a, const auto& b) { return a.first.getStart() < b.first.getStart(); });

    const auto font = theme::font (size);
    int pos = 0;
    for (const auto& [range, term] : links)
    {
        attributed.append (text.substring (pos, range.getStart()), font, colour);
        attributed.append (text.substring (range.getStart(), range.getEnd()), font, theme::accent);
        pos = range.getEnd();
    }
    attributed.append (text.substring (pos), font, colour);
    attributed.setWordWrap (juce::AttributedString::byWord);
    attributed.setLineSpacing (2.0f);
}

void LinkText::build (int width)
{
    if (width == builtWidth)
        return;
    builtWidth = width;
    layout.createLayout (attributed, (float) juce::jmax (10, width));
}

int LinkText::heightForWidth (int width)
{
    build (width);
    return (int) std::ceil (layout.getHeight()) + 2;
}

void LinkText::paint (juce::Graphics& g)
{
    build (getWidth());
    layout.draw (g, getLocalBounds().toFloat());
}

juce::String LinkText::termAt (juce::Point<float> p) const
{
    for (const auto& line : layout)
        for (const auto* run : line.runs)
        {
            if (run->glyphs.isEmpty())
                continue;
            const float x0 = line.lineOrigin.x + run->glyphs.getReference (0).anchor.x;
            const auto& last = run->glyphs.getReference (run->glyphs.size() - 1);
            const float x1 = line.lineOrigin.x + last.anchor.x + last.width;
            const float y0 = line.lineOrigin.y - line.ascent, y1 = line.lineOrigin.y + line.descent;
            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1)
                for (const auto& [range, term] : links)
                    if (range.intersects (run->stringRange))
                        return term;
        }
    return {};
}

void LinkText::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (termAt (e.position).isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void LinkText::mouseUp (const juce::MouseEvent& e)
{
    const auto t = termAt (e.position);
    if (t.isNotEmpty() && onTerm)
        onTerm (t);
}

// ---------------------------------------------------------------- Card

Card::Card (const juce::String& t, const juce::String& b, std::vector<Button> bs, std::function<void (const juce::String&)> onTerm, bool acc)
    : title (t), accent (acc)
{
    body = std::make_unique<LinkText> (b, 13.0f, theme::text, std::move (onTerm));
    addAndMakeVisible (*body);
    for (auto& bt : bs)
    {
        auto* btn = buttons.add (new juce::TextButton (bt.label));
        btn->onClick = bt.action;
        addAndMakeVisible (btn);
    }
}

int Card::layoutForWidth (int width)
{
    const int inner = width - 20;
    int h = 10;
    if (title.isNotEmpty())
        h += 20;
    h += body->heightForWidth (inner);
    if (! buttons.isEmpty())
        h += 30;
    return h + 10;
}

void Card::resized()
{
    auto r = getLocalBounds().reduced (10);
    if (title.isNotEmpty())
        r.removeFromTop (20);
    if (! buttons.isEmpty())
    {
        auto row = r.removeFromBottom (24);
        for (auto* b : buttons)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (theme::font (12.0f, true), b->getButtonText()) + 20;
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
        r.removeFromBottom (6);
    }
    body->setBounds (r);
}

void Card::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (r, 6.0f);
    if (accent)
    {
        g.setColour (theme::accent);
        g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
    }
    if (title.isNotEmpty())
    {
        g.setColour (theme::text);
        g.setFont (theme::font (13.5f, true));
        g.drawText (title, getLocalBounds().reduced (10).removeFromTop (18), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------- CoachPanel

CoachPanel::CoachPanel (EditorContext& c, MainView& m) : ctx (c), main (m), content (coach::Content::get())
{
    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].setTitle (juce::String (tabNames[i]) + " tab");
        tabs[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabs[i]);
    }
    quiet.setClickingTogglesState (true);
    quiet.setToggleState (ctx.proc.coachSettings().quiet, juce::dontSendNotification);
    quiet.setTitle ("Quiet mode");
    quiet.onClick = [this]
    {
        auto& s = ctx.proc.coachSettings();
        s.quiet = quiet.getToggleState();
        s.save();
        ctx.proc.tipEngine().setQuiet (s.quiet);
        lastSignature.clear();
        rebuild();
    };
    addAndMakeVisible (quiet);
    earBtn.setClickingTogglesState (true);
    earBtn.setTitle ("Ear trainer");
    earBtn.onClick = [this]
    {
        if (onEarTrainerToggled)
            onEarTrainerToggled (earBtn.getToggleState());
    };
    addAndMakeVisible (earBtn);
    earBtn.setVisible (false); // shown when the ear trainer exists (Phase 7)

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    ctx.showGlossaryTerm = [this] (const juce::String& t) { showGlossaryTerm (t); };
    ctx.onUiEvent = [this] (const juce::String& e)
    {
        ctx.proc.recipeRunner().event (e);
        if (e == "bypass toggled" || e.startsWith ("delta") || e.startsWith ("listen") || e.startsWith ("step") || e == "track type")
            timerCallback();
    };
    auto& runner = ctx.proc.recipeRunner();
    runner.onStepChanged = [this]
    {
        applyRecipeHighlight();
        requestRebuild();
    };
    runner.onFinished = [this] (const juce::String& id)
    {
        auto& s = ctx.proc.coachSettings();
        s.recipesDone.addIfNotAlreadyThere (id);
        s.save();
        main.setStep (ctx.proc.getSession().workflowStep);
        requestRebuild();
    };

    startMs = juce::Time::getMillisecondCounterHiRes();
    showTab (ctx.proc.recipeRunner().isRunning() ? 1 : 0);
    startTimerHz (4);
}

CoachPanel::~CoachPanel()
{
    stopTimer();
    ctx.showGlossaryTerm = nullptr;
    ctx.onUiEvent = nullptr;
    ctx.proc.recipeRunner().onStepChanged = nullptr;
    ctx.proc.recipeRunner().onFinished = nullptr;
}

bool CoachPanel::shouldStartVisible() const
{
    return ctx.proc.coachSettings().coachVisible;
}

void CoachPanel::showTab (int t)
{
    tab = t;
    for (int i = 0; i < 4; ++i)
        tabs[i].setToggleState (i == tab, juce::dontSendNotification);
    lastSignature.clear();
    rebuild();
    viewport.setViewPosition (0, 0);
}

void CoachPanel::showGlossaryTerm (const juce::String& term)
{
    glossaryFocus = term;
    showTab (3);
    for (auto* c : cards)
        if (c->getTitle() == term)
            viewport.setViewPosition (0, c->getY() - 4);
}

void CoachPanel::requestRebuild()
{
    lastSignature.clear();
    juce::Component::SafePointer<CoachPanel> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr)
            safe->rebuild();
    });
}

void CoachPanel::requestTab (int t)
{
    juce::Component::SafePointer<CoachPanel> safe (this);
    juce::MessageManager::callAsync ([safe, t]
    {
        if (safe != nullptr)
            safe->showTab (t);
    });
}

void CoachPanel::addCard (std::unique_ptr<Card> c)
{
    list.addAndMakeVisible (*c);
    cards.add (c.release());
}

juce::String CoachPanel::nowSignature() const
{
    juce::String sig;
    sig << tab << "|";
    const int b = ctx.selection.primary();
    dsp::EngineParams ep;
    ctx.proc.readParams (ep);
    const auto s = ctx.proc.getSession();
    if (b >= 0 && ep.bands[(size_t) b].on)
        sig << coach::explain (ep.bands[(size_t) b], s.trackType, ctx.proc.stats().bands[(size_t) b].activity.load(), ctx.proc.isMono() ? 1 : 2);
    for (const auto& t : ctx.proc.tipEngine().visible())
        sig << "|" << t.id << t.text;
    sig << "|" << s.workflowStep << (ctx.proc.coachSettings().firstRunDone ? 1 : 0);
    const auto& r = ctx.proc.recipeRunner();
    sig << "|" << (r.isRunning() ? r.recipe()->id + juce::String (r.stepIndex()) : juce::String()) << s.trackType;
    return sig;
}

void CoachPanel::timerCallback()
{
    const double t = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto state = coach::sampleProcessor (ctx.proc, t);
    ctx.proc.tipEngine().update (state);
    ctx.proc.recipeRunner().update (state);
    if (tab == 0 || tab == 1)
    {
        const auto sig = nowSignature();
        if (sig != lastSignature)
        {
            lastSignature = sig;
            rebuild();
        }
    }
}

void CoachPanel::rebuild()
{
    const int scroll = viewport.getViewPositionY();
    cards.clear();
    list.removeAllChildren();
    switch (tab)
    {
        case 0: buildNow(); break;
        case 1: buildRecipes(); break;
        case 2: buildLessons(); break;
        default: buildGlossary(); break;
    }
    resized();
    if (tab != 3)
        viewport.setViewPosition (0, scroll);
}

void CoachPanel::buildNow()
{
    auto term = [this] (const juce::String& t) { showGlossaryTerm (t); };
    auto& settings = ctx.proc.coachSettings();

    if (! settings.firstRunDone)
    {
        if (const auto* r = content.recipe ("rec_first_eq"))
            addCard (std::make_unique<Card> (r->title, r->steps.front().text,
                                             std::vector<Card::Button> { { "Start", [this]
                                                                           {
                                                                               auto& s = ctx.proc.coachSettings();
                                                                               s.firstRunDone = true;
                                                                               s.save();
                                                                               ctx.proc.recipeRunner().start ("rec_first_eq", coach::sampleProcessor (ctx.proc, 0.0));
                                                                               requestTab (1);
                                                                           } },
                                                                         { "Not now", [this]
                                                                           {
                                                                               auto& s = ctx.proc.coachSettings();
                                                                               s.firstRunDone = true;
                                                                               s.save();
                                                                               requestRebuild();
                                                                           } } },
                                             term, true));
    }

    const int b = ctx.selection.primary();
    dsp::EngineParams ep;
    ctx.proc.readParams (ep);
    const auto s = ctx.proc.getSession();
    if (b >= 0 && ep.bands[(size_t) b].on)
        addCard (std::make_unique<Card> ("Band " + juce::String (b + 1),
                                         coach::explain (ep.bands[(size_t) b], s.trackType, ctx.proc.stats().bands[(size_t) b].activity.load(), ctx.proc.isMono() ? 1 : 2),
                                         std::vector<Card::Button> {}, term));

    for (const auto& tip : ctx.proc.tipEngine().visible (2))
    {
        const auto id = tip.id;
        addCard (std::make_unique<Card> ("", tip.text,
                                         std::vector<Card::Button> { { "Got it", [this, id]
                                                                       {
                                                                           ctx.proc.tipEngine().gotIt (id);
                                                                           requestRebuild();
                                                                       } },
                                                                     { "Don't show again", [this, id]
                                                                       {
                                                                           auto& st = ctx.proc.coachSettings();
                                                                           st.tipsNever.addIfNotAlreadyThere (id);
                                                                           st.save();
                                                                           ctx.proc.tipEngine().setNeverShow (st.tipsNever);
                                                                           requestRebuild();
                                                                       } } },
                                         term, true));
    }

    const auto hint = content.stepHint (s.workflowStep);
    if (hint.isNotEmpty())
        addCard (std::make_unique<Card> ("", hint, std::vector<Card::Button> {}, term));
}

void CoachPanel::applyRecipeHighlight()
{
    auto& r = ctx.proc.recipeRunner();
    if (const auto* st = r.currentStep())
    {
        juce::StringArray h;
        if (st->highlight.isNotEmpty())
            h.add (st->highlight);
        main.setHighlights (h);
    }
}

void CoachPanel::buildRecipes()
{
    auto term = [this] (const juce::String& t) { showGlossaryTerm (t); };
    auto& runner = ctx.proc.recipeRunner();
    const auto trackType = ctx.proc.getSession().trackType;

    if (runner.isRunning())
    {
        const auto* r = runner.recipe();
        const auto* st = runner.currentStep();
        std::vector<Card::Button> buttons;
        buttons.push_back ({ "Back", [this] { ctx.proc.recipeRunner().back (coach::sampleProcessor (ctx.proc, 0.0)); } });
        const bool hasCheck = runner.stepHasCheck();
        buttons.push_back ({ runner.isLastStep() ? "Done" : (hasCheck ? "Skip" : "Next"), [this] { ctx.proc.recipeRunner().next (coach::sampleProcessor (ctx.proc, 0.0)); } });
        buttons.push_back ({ "Stop", [this]
                             {
                                 ctx.proc.recipeRunner().stop();
                                 main.setStep (ctx.proc.getSession().workflowStep);
                             } });
        addCard (std::make_unique<Card> (r->title + "  (" + juce::String (runner.stepIndex() + 1) + "/" + juce::String ((int) r->steps.size()) + ")",
                                         st != nullptr ? st->text : juce::String(), std::move (buttons), term, true));
        return;
    }

    // Suggested for this track type first.
    std::vector<const coach::Recipe*> order;
    for (const auto& r : content.recipes)
        if (r.trackTypes.contains (trackType) || r.trackTypes.contains ("all"))
            order.push_back (&r);
    for (const auto& r : content.recipes)
        if (std::find (order.begin(), order.end(), &r) == order.end())
            order.push_back (&r);

    const auto& done = ctx.proc.coachSettings().recipesDone;
    for (const auto* r : order)
    {
        const auto id = r->id;
        const juce::String title = r->title + (done.contains (id) ? juce::String::fromUTF8 ("  \xe2\x9c\x93") : juce::String());
        addCard (std::make_unique<Card> (title, r->steps.front().text,
                                         std::vector<Card::Button> { { "Start", [this, id]
                                                                       {
                                                                           ctx.proc.recipeRunner().start (id, coach::sampleProcessor (ctx.proc, 0.0));
                                                                           applyRecipeHighlight();
                                                                           requestRebuild();
                                                                       } } },
                                         term));
    }
}

void CoachPanel::buildLessons()
{
    auto term = [this] (const juce::String& t) { showGlossaryTerm (t); };
    for (const auto& l : content.lessons)
    {
        std::vector<Card::Button> buttons;
        if (l.tryIt.isNotEmpty())
        {
            const auto id = l.id;
            buttons.push_back ({ "Try it", [this, id]
                                 {
                                     // Adds new bands only; never edits existing ones. Undoable.
                                     dsp::BandParams bp;
                                     bp.shape = dsp::Shape::Bell;
                                     bp.freq = 1000.0f;
                                     bp.q = 1.0f;
                                     if (id == "les_sweep")
                                     {
                                         bp.gain = 10.0f;
                                         bp.q = 8.0f;
                                     }
                                     const int b = ctx.proc.addBand (bp, "Try it");
                                     if (b >= 0)
                                         ctx.selection.selectOnly (b);
                                 } });
        }
        addCard (std::make_unique<Card> (l.title, l.text, std::move (buttons), term));
    }
    auto& s = ctx.proc.coachSettings();
    bool changed = false;
    for (const auto& l : content.lessons)
        if (! s.lessonsRead.contains (l.id))
        {
            s.lessonsRead.add (l.id);
            changed = true;
        }
    if (changed)
        s.save();
}

void CoachPanel::buildGlossary()
{
    auto term = [this] (const juce::String& t) { showGlossaryTerm (t); };
    for (const auto& g : content.glossary)
    {
        auto c = std::make_unique<Card> (g.term, g.definition, std::vector<Card::Button> {}, term, g.term == glossaryFocus);
        c->setTitle (g.term);
        addCard (std::move (c));
    }
}

void CoachPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void CoachPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (24);
    for (auto& t : tabs)
        t.setBounds (top.removeFromLeft (r.getWidth() / 4).reduced (1, 0));
    r.removeFromTop (4);
    auto row = r.removeFromTop (22);
    quiet.setBounds (row.removeFromRight (60));
    if (earBtn.isVisible())
        earBtn.setBounds (row.removeFromLeft (92));
    r.removeFromTop (6);
    if (earTrainerView != nullptr && earTrainerView->isVisible())
    {
        earTrainerView->setBounds (r);
        viewport.setVisible (false);
        return;
    }
    viewport.setVisible (true);
    viewport.setBounds (r);

    const int w = r.getWidth() - 8;
    int y = 0;
    for (auto* c : cards)
    {
        const int h = c->layoutForWidth (w);
        c->setBounds (0, y, w, h);
        y += h + 8;
    }
    list.setSize (w, juce::jmax (y, r.getHeight()));
}
} // namespace lens::gui
