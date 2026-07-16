#include "PluginEditor.h"

Nebula2AudioProcessorEditor::Nebula2AudioProcessorEditor(Nebula2AudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(420, 260);
}

void Nebula2AudioProcessorEditor::paint(juce::Graphics& g)
{
    // No UI yet (that's Phase 5) - so the placeholder earns its keep by showing the note
    // map. Keep this text ASCII: source is compiled as UTF-8 (see /utf-8 in CMakeLists),
    // but plain literals are the safest thing to rely on here.
    g.fillAll(juce::Colour(0xff0b0e16));

    auto area = getLocalBounds().reduced(16);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f));
    g.drawFittedText("Nebula2 - drum engine", area.removeFromTop(28), juce::Justification::centredLeft, 1);

    g.setColour(juce::Colour(0xff9aa3bd));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("no UI yet - play these MIDI notes:", area.removeFromTop(20), juce::Justification::centredLeft, 1);

    area.removeFromTop(8);

    struct Row { const char* note; const char* name; };
    const Row rows[] = {
        { "36  C1",  "Kick" },   { "38  D1",  "Snare" },
        { "39  D#1", "Clap" },   { "42  F#1", "Hat (closed)" },
        { "46  A#1", "Hat (open)" }, { "45  A1", "Tom" },
        { "37  C#1", "Rim" },    { "75  D#4", "Perc" },
    };

    g.setFont(juce::FontOptions(13.0f));
    for (const auto& r : rows)
    {
        auto line = area.removeFromTop(18);
        g.setColour(juce::Colour(0xff3fe0d4));
        g.drawFittedText(r.note, line.removeFromLeft(70), juce::Justification::centredLeft, 1);
        g.setColour(juce::Colours::white);
        g.drawFittedText(r.name, line, juce::Justification::centredLeft, 1);
    }
}

void Nebula2AudioProcessorEditor::resized()
{
}
