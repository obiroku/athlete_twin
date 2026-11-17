#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// Coach text, loaded from the JSON that tools/lessons_to_json.py makes
// from LESSONS.md (embedded with BinaryData). Headless.
namespace lens::coach
{
struct Zone
{
    juce::String name;
    double lo = 0.0, hi = 0.0; // hi = 0 means "and up"
};

struct WidthPhrase
{
    double min = 0.0, max = 0.0;
    juce::String text;
};

struct Tip
{
    juce::String id, when, text;
};

struct RecipeStep
{
    juce::String highlight, text, check;
};

struct Recipe
{
    juce::String id, title;
    juce::StringArray trackTypes;
    std::vector<RecipeStep> steps;
};

struct Lesson
{
    juce::String id, title, text, tryIt;
};

struct GlossaryEntry
{
    juce::String term, definition;
};

struct EarLevel
{
    int level = 1;
    juce::String band, choices, pass;
};

class Content
{
public:
    // Loads from the embedded BinaryData.
    static const Content& get();
    // Loads from a folder of JSON files (tests).
    static Content fromFolder (const juce::File&);

    juce::String tooltip (const juce::String& control) const;
    juce::String template_ (const juce::String& key) const;
    juce::String stepHint (const juce::String& step) const; // "CLEAN" → step_clean
    const std::vector<Zone>& zonesFor (const juce::String& trackType) const;
    juce::String zoneAt (const juce::String& trackType, double freq) const;
    const Tip* tip (const juce::String& id) const;
    const Recipe* recipe (const juce::String& id) const;
    const Lesson* lesson (const juce::String& id) const;

    std::vector<WidthPhrase> width;
    juce::StringPairArray templates;
    juce::StringPairArray steps;
    juce::StringPairArray controls;
    std::vector<std::pair<juce::String, std::vector<Zone>>> zones;
    std::vector<Tip> tips;
    std::vector<Recipe> recipes;
    std::vector<Lesson> lessons;
    std::vector<GlossaryEntry> glossary;
    std::vector<EarLevel> earLevels;
    juce::StringPairArray earMessages;

    // Every ID (templates, steps, tips, recipes, lessons) for the content check.
    juce::StringArray allIds() const;

private:
    void parse (const juce::String& name, const juce::var& json);
};
} // namespace lens::coach
