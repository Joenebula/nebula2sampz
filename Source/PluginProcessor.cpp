#include "PluginProcessor.h"
#include "PluginEditor.h"

Nebula2AudioProcessor::Nebula2AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void Nebula2AudioProcessor::prepareToPlay(double, int)
{
}

void Nebula2AudioProcessor::releaseResources()
{
}

bool Nebula2AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Nebula2AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midiMessages.clear();
}

juce::AudioProcessorEditor* Nebula2AudioProcessor::createEditor()
{
    return new Nebula2AudioProcessorEditor(*this);
}

// This creates the instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Nebula2AudioProcessor();
}
