#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

Nebula2AudioProcessor::Nebula2AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "PARAMS", Nebula2::createParameterLayout())
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

void Nebula2AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Persist the whole parameter tree. Structured/blob state (step order, grid matrix,
    // drum patterns) will be added as extra child branches of this tree in later phases.
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void Nebula2AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
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
