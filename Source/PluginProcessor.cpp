#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"

Nebula2AudioProcessor::Nebula2AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "PARAMS", Nebula2::createParameterLayout())
{
    masterParam  = apvts.getRawParameterValue(Nebula2::ParamID::master);
    limiterParam = apvts.getRawParameterValue(Nebula2::ParamID::limiter);
}

void Nebula2AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    masterProcessor.prepare(spec);
    masterProcessor.reset();

    // Preallocate the layer buses here (never in the audio callback).
    sampleBus.setSize(2, samplesPerBlock, false, false, true);
    drumBus.setSize(2, samplesPerBlock, false, false, true);
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

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    buffer.clear();

    // Layer buses -> main. Silent until Phase 3 renders sources into them, but the
    // routing is real now so Phase 3 only has to fill the buffers.
    sampleBus.clear();
    drumBus.clear();
    for (int c = 0; c < numChannels && c < 2; ++c)
    {
        buffer.addFrom(c, 0, sampleBus, c, 0, numSamples);
        buffer.addFrom(c, 0, drumBus,   c, 0, numSamples);
    }

    // Host transport snapshot (musical time) for the future scheduler/UI.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            transport = Nebula2::readTransport(*pos);

    // Master chain: gain -> limiter -> safety clamp.
    const float g   = masterParam  != nullptr ? masterParam->load()          : 0.9f;
    const bool  lim = limiterParam != nullptr ? limiterParam->load() > 0.5f   : true;
    masterProcessor.process(buffer, g, lim);

    midiMessages.clear();
}

void Nebula2AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
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
