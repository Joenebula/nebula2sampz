#include "PluginEditor.h"

Nebula2AudioProcessorEditor::Nebula2AudioProcessorEditor(Nebula2AudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(400, 300);
}

void Nebula2AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText("Nebula2 — Phase 0 (no UI yet)", getLocalBounds(), juce::Justification::centred, 1);
}

void Nebula2AudioProcessorEditor::resized()
{
}
