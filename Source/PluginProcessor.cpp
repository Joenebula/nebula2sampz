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
    sliceModeParam   = apvts.getRawParameterValue(Nebula2::ParamID::sliceMode);
    sliceCountParam  = apvts.getRawParameterValue(Nebula2::ParamID::sliceCount);
    sensitivityParam = apvts.getRawParameterValue(Nebula2::ParamID::sensitivity);
}

int Nebula2AudioProcessor::sliceCountFromChoice(int choiceIndex) noexcept
{
    static constexpr int counts[] = { 4, 8, 16, 32, 64 };
    return counts[juce::jlimit(0, 4, choiceIndex)];
}

void Nebula2AudioProcessor::handleAsyncUpdate()
{
    // Message thread: everything here allocates, which is exactly why it's here.

    // Restore a sample saved with the project.
    juce::String pathToLoad;
    {
        const juce::ScopedLock sl(pendingPathLock);
        pathToLoad = pendingSamplePath;
        pendingSamplePath.clear();
    }
    if (pathToLoad.isNotEmpty())
    {
        const juce::File f(pathToLoad);
        const bool ok = f.existsAsFile() && sampleLayer.loadFile(f);
        if (auto* ed = dynamic_cast<Nebula2AudioProcessorEditor*>(getActiveEditor()))
            ed->sampleReSliced();      // refresh the readout/waveform either way
        juce::ignoreUnused(ok);        // a missing file just leaves the layer empty
    }

    const auto wanted = (Nebula2::ReverbChar) juce::jlimit(0, 4, wantedRevChar.load());
    if (wanted != space.getCharacter())
        space.setCharacter(wanted);

    Nebula2::SampleLayer::SliceSettings s;
    s.transient   = wantedSliceMode.load() == 1;
    s.count       = wantedSliceCount.load();
    s.sensitivity = wantedSensitivity.load();

    const auto cur = sampleLayer.getSliceSettings();
    if (s.transient != cur.transient || s.count != cur.count
        || std::abs(s.sensitivity - cur.sensitivity) > 1.0e-4f)
    {
        sampleLayer.setSliceSettings(s);      // re-slices the same audio, republishes
        if (auto* ed = dynamic_cast<Nebula2AudioProcessorEditor*>(getActiveEditor()))
            ed->sampleReSliced();
    }
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
    sampleLayer.prepare(sampleRate, samplesPerBlock);
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

    // Host transport FIRST — noteOn below computes its time-stretch from the tempo, so a
    // stale BPM here would mis-stretch the first slice of every block.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            transport = Nebula2::readTransport(*pos);
    sampleLayer.setHostBpm(transport.bpm);

    // Both layers render in sub-blocks split at MIDI event positions, so hits land
    // sample-accurately rather than snapping to the block grid. Notes route by range:
    // GM drum notes -> the kit, note 84 (C5) and up -> sample slices.
    {
        int cursor = 0;
        for (const auto meta : midiMessages)
        {
            const int pos = juce::jlimit(0, numSamples, meta.samplePosition);
            if (pos > cursor)
            {
                drumKit.render(drumBus, cursor, pos - cursor);
                sampleLayer.render(sampleBus, cursor, pos - cursor);
                cursor = pos;
            }

            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                drumKit.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
                sampleLayer.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            }
            else if (msg.isNoteOff())
            {
                // Drums are one-shots (a kick doesn't stop when you lift the key), but a
                // chop is gated by its note length — that's what makes it a slicer.
                sampleLayer.noteOff(msg.getNoteNumber());
            }
        }
        if (cursor < numSamples)
        {
            drumKit.render(drumBus, cursor, numSamples - cursor);
            sampleLayer.render(sampleBus, cursor, numSamples - cursor);
        }
    }

    // Layer buses -> main.
    for (int c = 0; c < numChannels && c < 2; ++c)
    {
        buffer.addFrom(c, 0, sampleBus, c, 0, numSamples);
        buffer.addFrom(c, 0, drumBus,   c, 0, numSamples);
    }

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

    // Slicing settings changed? Re-slicing allocates — hand it to the message thread.
    {
        const int mode  = sliceModeParam  != nullptr ? (int) sliceModeParam->load() : 0;
        const int count = sliceCountFromChoice(sliceCountParam != nullptr ? (int) sliceCountParam->load() : 2);
        const float sens = sensitivityParam != nullptr ? sensitivityParam->load() : 0.5f;

        if (mode != wantedSliceMode.load() || count != wantedSliceCount.load()
            || std::abs(sens - wantedSensitivity.load()) > 1.0e-4f)
        {
            wantedSliceMode.store(mode);
            wantedSliceCount.store(count);
            wantedSensitivity.store(sens);
            triggerAsyncUpdate();
        }
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
    auto state = apvts.copyState();

    // State is a contract: the parameters alone aren't the patch. Without the sample, a
    // saved project reopens with all your knobs and NO BREAK. Store where it came from.
    // (Path, not the audio itself — so moving/renaming the file breaks the link. That's the
    // usual sampler trade-off; embedding the audio in every project is the alternative.)
    auto sampleNode = state.getOrCreateChildWithName("SAMPLE", nullptr);
    sampleNode.setProperty("path", sampleLayer.getSourcePath(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void Nebula2AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml(*xml);

    // Restore the sample. Decoding allocates and may be called off the message thread by
    // the host, so record it and let handleAsyncUpdate() do the load.
    if (auto sampleNode = tree.getChildWithName("SAMPLE"); sampleNode.isValid())
    {
        const juce::String path = sampleNode.getProperty("path").toString();
        if (path.isNotEmpty() && path != sampleLayer.getSourcePath())
        {
            const juce::ScopedLock sl(pendingPathLock);
            pendingSamplePath = path;
            triggerAsyncUpdate();
        }
    }

    apvts.replaceState(tree);
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
