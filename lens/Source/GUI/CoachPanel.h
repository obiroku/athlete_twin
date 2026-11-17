#pragma once

#include "EditorContext.h"
#include "Theme.h"

#include "Coach/CoachContent.h"

namespace lens::gui
{
class MainView;

// Text with glossary terms as clickable links.
class LinkText final : public juce::Component
{
public:
    LinkText (const juce::String& text, float fontSize, juce::Colour colour, std::function<void (const juce::String&)> onTerm);
    int heightForWidth (int width);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String termAt (juce::Point<float>) const;
    void build (int width);

    juce::String text;
    float size;
    juce::Colour colour;
    std::function<void (const juce::String&)> onTerm;
    juce::AttributedString attributed;
    juce::TextLayout layout;
    std::vector<std::pair<juce::Range<int>, juce::String>> links;
    int builtWidth = -1;
};

// One Coach card: optional title, linked text, buttons.
class Card final : public juce::Component
{
public:
    struct Button
    {
        juce::String label;
        std::function<void()> action;
    };
    Card (const juce::String& title, const juce::String& body, std::vector<Button> buttons, std::function<void (const juce::String&)> onTerm,
          bool accent = false);
    int layoutForWidth (int width);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    std::unique_ptr<LinkText> body;
    juce::OwnedArray<juce::TextButton> buttons;
    bool accent;
};

class CoachPanel final : public juce::Component,
                         private juce::Timer
{
public:
    CoachPanel (EditorContext&, MainView&);
    ~CoachPanel() override;

    bool shouldStartVisible() const;
    void paint (juce::Graphics&) override;
    void resized() override;

    void showTab (int tab);
    void showGlossaryTerm (const juce::String& term);

    // Ear trainer (Phase 7) hooks into the panel.
    std::function<void (bool)> onEarTrainerToggled;
    juce::Component* earTrainerView = nullptr;

private:
    void timerCallback() override;
    void rebuild();
    void requestRebuild();
    void requestTab (int);
    void addCard (std::unique_ptr<Card>);
    void buildNow();
    void buildRecipes();
    void buildLessons();
    void buildGlossary();
    void applyRecipeHighlight();
    juce::String nowSignature() const;

    EditorContext& ctx;
    MainView& main;
    const coach::Content& content;

    juce::TextButton tabs[4];
    juce::ToggleButton quiet { "Quiet" }, earBtn { "Ear trainer" };
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Card> cards;
    int tab = 0;
    juce::String lastSignature, glossaryFocus;
    double startMs = 0.0;
};
} // namespace lens::gui
