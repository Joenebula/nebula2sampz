#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"

Nebula2AudioProcessor::Nebula2AudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "PARAMS", Nebula2::createParameterLayout())
{
    masterParam    = apvts.getRawParameterValue(Nebula2::ParamID::master);
    limiterParam   = apvts.getRawParameterValue(Nebula2::ParamID::limiter);
    driveParam     = apvts.getRawParameterValue(Nebula2::ParamID::drive);
    driveCharParam = apvts.getRawParameterValue(Nebula2::ParamID::driveChar);
    crushParam     = apvts.getRawParameterValue(Nebula2::ParamID::crush);
    squeezeParam   = apvts.getRawParameterValue(Nebula2::ParamID::squeeze);
    toneParam      = apvts.getRawParameterValue(Nebula2::ParamID::tone);
    widthParam     = apvts.getRawParameterValue(Nebula2::ParamID::width);
    fxOnParam      = apvts.getRawParameterValue(Nebula2::ParamID::fxOn);
    revMixParam    = apvts.getRawParameterValue(Nebula2::ParamID::revMix);
    revCharParam   = apvts.getRawParameterValue(Nebula2::ParamID::revChar);
    dlyMixParam    = apvts.getRawParameterValue(Nebula2::ParamID::dlyMix);
    dlyFbParam     = apvts.getRawParameterValue(Nebula2::ParamID::dlyFb);
    dlySyncParam   = apvts.getRawParameterValue(Nebula2::ParamID::dlySync);
    spaceOnParam   = apvts.getRawParameterValue(Nebula2::ParamID::spaceOn);
}

void Nebula2AudioProcessor::handleAsyncUpdate()
{
    // Message thread: safe to synthesise + load a new impulse response here.
    const auto wanted = (Nebula2::ReverbChar) juce::jlimit(0, 4, wantedRevChar.load());
    if (wanted != space.getCharacter())
        space.setCharacter(wanted);
}

void Nebula2AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    masterProcessor.prepare(spec);
    masterProcessor.reset();
    colourChain.prepare(spec);
    colourChain.reset();
    space.prepare(spec);
    space.reset();

    // Preallocate the layer buses here (never in the audio callback).
    sampleBus.setSize(2, samplesPerBlock, false, false, true);
    drumBus.setSize(2, samplesPerBlock, false, false, true);

    // Pre-render the drum kit (heavy + allocates — must not happen on the audio thread).
    drumKit.prepare(sampleRate);
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
    sampleBus.clear();
    drumBus.clear();

    // Drum layer: render in sub-blocks split at MIDI event positions, so hits land
    // sample-accurately rather than snapping to the block grid.
    {
        int cursor = 0;
        for (const auto meta : midiMessages)
        {
            const int pos = juce::jlimit(0, numSamples, meta.samplePosition);
            if (pos > cursor) { drumKit.render(drumBus, cursor, pos - cursor); cursor = pos; }

            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                drumKit.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        }
        if (cursor < numSamples) drumKit.render(drumBus, cursor, numSamples - cursor);
    }

    // Layer buses -> main. (sampleBus stays silent until the slicer lands.)
    for (int c = 0; c < numChannels && c < 2; ++c)
    {
        buffer.addFrom(c, 0, sampleBus, c, 0, numSamples);
        buffer.addFrom(c, 0, drumBus,   c, 0, numSamples);
    }

    // Host transport snapshot (musical time) for the future scheduler/UI.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            transport = Nebula2::readTransport(*pos);

    // Colour: drive -> crush/width -> squeeze -> tone, on the summed layers.
    {
        Nebula2::ColourChain::Params cp;
        cp.drive     = driveParam     != nullptr ? driveParam->load()     : 0.0f;
        cp.crush     = crushParam     != nullptr ? crushParam->load()     : 0.0f;
        cp.squeeze   = squeezeParam   != nullptr ? squeezeParam->load()   : 0.0f;
        cp.tone      = toneParam      != nullptr ? toneParam->load()      : 100.0f;
        cp.width     = widthParam     != nullptr ? widthParam->load()     : 100.0f;
        cp.driveChar = driveCharParam != nullptr ? (int) driveCharParam->load() : 0;
        cp.on        = fxOnParam      != nullptr ? fxOnParam->load() > 0.5f : true;
        colourChain.process(buffer, cp);
    }

    // Space: parallel reverb + tempo-synced delay send (dry is preserved).
    {
        const int wantChar = revCharParam != nullptr ? (int) revCharParam->load() : 1;
        if (wantChar != wantedRevChar.load())
        {
            wantedRevChar.store(wantChar);
            triggerAsyncUpdate();          // rebuild the IR off the audio thread
        }

        Nebula2::SpaceProcessor::Params sp;
        sp.revMix   = revMixParam  != nullptr ? revMixParam->load()  : 0.0f;
        sp.dlyMix   = dlyMixParam  != nullptr ? dlyMixParam->load()  : 0.0f;
        sp.dlyFb    = dlyFbParam   != nullptr ? dlyFbParam->load()   : 40.0f;
        sp.dlySync  = (Nebula2::DelaySync) (dlySyncParam != nullptr ? juce::jlimit(0, 5, (int) dlySyncParam->load()) : 2);
        sp.on       = spaceOnParam != nullptr ? spaceOnParam->load() > 0.5f : true;
        sp.bpm      = transport.bpm;       // musical time, from the host
        space.process(buffer, sp);
    }

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
