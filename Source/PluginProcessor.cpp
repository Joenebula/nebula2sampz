#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "ParameterIDs.h"
#include "Presets.h"

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
    pumpParam      = apvts.getRawParameterValue(Nebula2::ParamID::pump);
    gateParam      = apvts.getRawParameterValue(Nebula2::ParamID::gate);
    reverseParam   = apvts.getRawParameterValue(Nebula2::ParamID::reverse);
    stutterParam   = apvts.getRawParameterValue(Nebula2::ParamID::stutter);
    shatterParam   = apvts.getRawParameterValue(Nebula2::ParamID::shatter);
    pitchUpParam   = apvts.getRawParameterValue(Nebula2::ParamID::pitchUp);
    pitchDownParam = apvts.getRawParameterValue(Nebula2::ParamID::pitchDown);
    resonateParam  = apvts.getRawParameterValue(Nebula2::ParamID::resonate);
    resoKeyParam   = apvts.getRawParameterValue(Nebula2::ParamID::resoKey);
    resoScaleParam = apvts.getRawParameterValue(Nebula2::ParamID::resoScale);
    smpVolParam    = apvts.getRawParameterValue(Nebula2::ParamID::smpVol);
    fxOnParam      = apvts.getRawParameterValue(Nebula2::ParamID::fxOn);
    revMixParam    = apvts.getRawParameterValue(Nebula2::ParamID::revMix);
    revCharParam   = apvts.getRawParameterValue(Nebula2::ParamID::revChar);
    revSizeParam   = apvts.getRawParameterValue(Nebula2::ParamID::revSize);
    dlyMixParam    = apvts.getRawParameterValue(Nebula2::ParamID::dlyMix);
    dlyFbParam     = apvts.getRawParameterValue(Nebula2::ParamID::dlyFb);
    dlySyncParam   = apvts.getRawParameterValue(Nebula2::ParamID::dlySync);
    dlyModeParam   = apvts.getRawParameterValue(Nebula2::ParamID::dlyMode);
    hauntParam     = apvts.getRawParameterValue(Nebula2::ParamID::haunt);
    spaceOnParam   = apvts.getRawParameterValue(Nebula2::ParamID::spaceOn);
    sliceModeParam   = apvts.getRawParameterValue(Nebula2::ParamID::sliceMode);
    sliceCountParam  = apvts.getRawParameterValue(Nebula2::ParamID::sliceCount);
    sensitivityParam = apvts.getRawParameterValue(Nebula2::ParamID::sensitivity);
    gridOnParam      = apvts.getRawParameterValue(Nebula2::ParamID::gridOn);
    gridStepsParam   = apvts.getRawParameterValue(Nebula2::ParamID::gridSteps);
    padOnParam       = apvts.getRawParameterValue(Nebula2::ParamID::padOn);
    padXParam        = apvts.getRawParameterValue(Nebula2::ParamID::padX);
    padYParam        = apvts.getRawParameterValue(Nebula2::ParamID::padY);
    morphMotionParam = apvts.getRawParameterValue(Nebula2::ParamID::morphMotion);
    morphRateParam   = apvts.getRawParameterValue(Nebula2::ParamID::morphRate);
    morphSizeParam   = apvts.getRawParameterValue(Nebula2::ParamID::morphSize);
    morphGlideParam  = apvts.getRawParameterValue(Nebula2::ParamID::morphGlide);

    rackOnParam = apvts.getRawParameterValue(Nebula2::ParamID::rackOn);
    {
        // Cached in the same order readRackDials() unpacks them. One list, one order — the
        // alternative is 33 named pointers and 33 chances to wire the wrong one.
        //
        // The list itself now lives in RackGraph (rackDspParamIds) so the editor's dial
        // table can be tested against it. It was local to this constructor, which meant
        // nothing outside could ask "is every one of these reachable?" — and five of them
        // were not.
        const auto& ids = Nebula2::rackDspParamIds();
        jassert(ids.size() == rackDialParams.size());
        for (size_t i = 0; i < rackDialParams.size() && i < ids.size(); ++i)
            rackDialParams[i] = apvts.getRawParameterValue(ids[i]);
    }
}

// Unpacks the cached params into the dial struct. Audio-thread safe: atomic loads only.
void Nebula2AudioProcessor::readRackDials() noexcept
{
    auto v = [this](int i, float fallback) noexcept
    {
        return rackDialParams[(size_t) i] != nullptr ? rackDialParams[(size_t) i]->load() : fallback;
    };

    rackDials.fltCut  = v(0, 1200.0f);
    rackDials.fltRes  = v(1, 1.0f);
    rackDials.fltType = (int) v(2, 0.0f);
    rackDials.lfoRate  = v(3, 1.5f);
    rackDials.lfoDepth = v(4, 50.0f);
    rackDials.lfoShape = (int) v(5, 0.0f);
    rackDials.phsRate  = v(6, 0.5f);
    rackDials.phsDepth = v(7, 75.0f);
    rackDials.phsFb    = v(8, 40.0f);
    rackDials.phsMix   = v(9, 50.0f);
    rackDials.choRate  = v(10, 0.8f);
    rackDials.choDepth = v(11, 50.0f);
    rackDials.choMix   = v(12, 50.0f);
    rackDials.cmbTune  = v(13, 180.0f);
    rackDials.cmbFb    = v(14, 80.0f);
    rackDials.cmbMix   = v(15, 50.0f);
    rackDials.fldDrive = v(16, 35.0f);
    rackDials.fldSym   = v(17, 0.0f);
    rackDials.fldMix   = v(18, 50.0f);
    rackDials.vowMorph = v(19, 0.0f);
    rackDials.vowSharp = v(20, 9.0f);
    rackDials.vowMix   = v(21, 50.0f);
    rackDials.echTime  = v(22, 320.0f);
    rackDials.echFb    = v(23, 55.0f);
    rackDials.echWow   = v(24, 37.0f);
    rackDials.echMix   = v(25, 50.0f);
    rackDials.outLvl   = v(26, 100.0f);
    for (int i = 0; i < 6; ++i)
        rackDials.eqGain[(size_t) i] = v(27 + i, 0.0f);
}

int Nebula2AudioProcessor::gridStepsFromChoice(int choiceIndex) noexcept
{
    static constexpr int counts[] = { 8, 16, 32 };
    return counts[juce::jlimit(0, 2, choiceIndex)];
}

int Nebula2AudioProcessor::getNumPrograms()
{
    return (int) Nebula2::getFactoryPresets().size();
}

const juce::String Nebula2AudioProcessor::getProgramName(int index)
{
    const auto& p = Nebula2::getFactoryPresets();
    return (index >= 0 && index < (int) p.size()) ? juce::String(p[(size_t) index].name) : juce::String();
}

void Nebula2AudioProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    currentProgram = index;
    // Resets every param to default, then applies the preset — INCLUDING the rack patch
    // and morph scenes, which live outside the APVTS and would otherwise survive a recall.
    Nebula2::applyPreset(apvts, index, rackGraph, rackLock, morphScenes, grid);
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
    juce::String orderToApply, fxToApply;
    {
        const juce::ScopedLock sl(pendingPathLock);
        orderToApply = pendingSliceOrder;
        fxToApply    = pendingSliceFx;
        pendingSliceOrder.clear();
        pendingSliceFx.clear();
    }
    if (pathToLoad.isNotEmpty())
    {
        const juce::File f(pathToLoad);
        const bool ok = f.existsAsFile() && sampleLayer.loadFile(f);
        // AFTER the load, for the reason given where it was stashed.
        if (orderToApply.isNotEmpty()) sampleLayer.sliceOrderFromString(orderToApply);
        if (fxToApply.isNotEmpty())    sampleLayer.sliceSettingsFromString(fxToApply);
        if (auto* ed = dynamic_cast<Nebula2AudioProcessorEditor*>(getActiveEditor()))
            ed->sampleReSliced();      // refresh the readout/waveform either way
        juce::ignoreUnused(ok);        // a missing file just leaves the layer empty
    }

    const auto wanted = (Nebula2::ReverbChar) juce::jlimit(0, 4, wantedRevChar.load());
    const float wantedSize = wantedRevSize.load();
    // One rebuild path for character AND size; setCharacterAndSize skips a redundant swap.
    // Load any IR that prepare() marked stale. This runs on the message thread in its OWN
    // call stack — never inside prepareToPlay, which is the whole point (see Reverb.cpp).
    space.reloadIrIfNeeded();

    space.setCharacterAndSize(wanted, wantedSize);

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

    // Layer faders. 20 ms, and set to their CURRENT value rather than ramping up from 0 —
    // otherwise every prepareToPlay (which a host does on every bus-layout change, not just
    // at startup) would fade the whole instrument in from silence.
    smpGain.reset(sampleRate, 0.02);
    smpGain.setCurrentAndTargetValue(smpVolParam != nullptr ? smpVolParam->load() / 100.0f : 1.0f);

    masterProcessor.prepare(spec);
    masterProcessor.reset();
    colourChain.prepare(spec);
    colourChain.reset();
    space.prepare(spec);
    space.reset();
    morph.prepare(spec);
    morph.reset();
    rackEngine.prepare(spec);
    rackEngine.reset();
    stepFx.prepare(spec);
    stepFx.reset();

    // Preallocate the layer buses here (never in the audio callback).
    sampleBus.setSize(2, samplesPerBlock, false, false, true);

    // Pre-render the drum kit (heavy + allocates — must not happen on the audio thread).
    sampleLayer.prepare(sampleRate, samplesPerBlock);

    // Load the reverb IR LATER, on the message thread, in a call stack of its own:
    // handleAsyncUpdate() calls space.reloadIrIfNeeded().
    //
    // Not from here, and this is the crash fix rather than tidiness. Loading inside prepare
    // queues work on the convolution's background queue and wakes its thread, so the NEXT
    // prepare's popAll races that thread's popAll on a single-CONSUMER FIFO, reads a slot
    // the other consumer already took, and calls an empty std::function.
    triggerAsyncUpdate();
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

    // Host transport FIRST — noteOn below computes its time-stretch from the tempo, so a
    // stale BPM here would mis-stretch the first slice of every block.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            transport = Nebula2::readTransport(*pos);

    // In-app audition (the app's own Play button). "The DAW takes over when it starts" —
    // detected by the transport POSITION advancing, NOT by getIsPlaying(). Some hosts
    // report isPlaying=true while stopped, and gating on it made the app loop clear itself
    // every block: the button showed Play forever and nothing sounded. Position advancing
    // is unambiguous — stopped means static ppq, rolling means it moves.
    const double hostPpq = transport.ppq;
    const bool hostRolling = Nebula2::hostIsRolling(hostPpq, lastHostPpq);
    lastHostPpq = hostPpq;
    hostTransportRolling.store(hostRolling);   // so the Preview button can tell the truth

    if (hostRolling)
    {
        // Host is the authority: silence the app loop on the block it starts, and let host
        // MIDI drive everything. The user's Play toggle stays as-is (it resumes if they stop
        // the DAW again) — we don't fight the button, we just suppress its loop.
        if (! auditionWasRolling) sampleLayer.noteOff(wholeBreakNote);
        auditionWasRolling = true;
        auditionPpq = 0.0;
    }
    else
    {
        auditionWasRolling = false;
        if (auditionActive.load())
        {
            // Synthesize a playing transport so the grid, morph shatter and delay-sync all
            // run while auditioning — the app should sound like the DAW will. Tempo = the
            // host's set BPM if we have one, else the break's detected tempo, else 120.
            double bpm = transport.bpm;
            if (bpm <= 0.0) bpm = sampleLayer.getDetectedBpm() > 0.0 ? sampleLayer.getDetectedBpm() : 120.0;
            transport.bpm     = bpm;
            transport.playing = true;
            transport.ppq     = auditionPpq;
            auditionPpq += (bpm / 60.0) * ((double) numSamples / juce::jmax(1.0, getSampleRate()));

            // Loop the whole break: re-trigger the instant nothing is sounding.
            if (! sampleLayer.isSounding())
                sampleLayer.noteOn(wholeBreakNote, 0.9f);
        }
        else
        {
            auditionPpq = 0.0;
        }
    }

    // --- FX grid ---
    // The grid does NOT write parameters (that was the prototype's slider-killing bug).
    // It reads the knobs and produces the amount for THIS step; the knob stays the single
    // source of truth and always sets the ceiling.
    //
    // This is resolved BEFORE the layers render because the Pitch +/- lanes act at
    // note-on: the transpose has to be in place before the chop for this step starts,
    // not a block later.
    const bool gridOn = gridOnParam != nullptr && gridOnParam->load() > 0.5f;
    int step = -1;
    if (gridOn)
    {
        const int steps = gridStepsFromChoice(gridStepsParam != nullptr ? (int) gridStepsParam->load() : 1);
        grid.setNumSteps(steps);
        step = Nebula2::FxGrid::stepAtPpq(transport.ppq, steps, 0.25);   // 1/16-note steps
    }
    // The PLAYHEAD is a different question from "which step is current". The step is always
    // derivable from the host's position, even parked — but drawing a lit column while
    // nothing is moving says "this is playing now", which is a lie, and it looked like
    // switching the grid on lit a random column. Report a playhead only when something is
    // actually running.
    const bool sounding = hostRolling || auditionActive.load();
    currentGridStep.store(sounding ? step : -1);

    // Reads a knob, then lets the grid gate it for this step (pass-through when off).
    const auto amt = [this, gridOn, step](std::atomic<float>* p, Nebula2::GridRow row, float fallback)
    {
        const float panel = p != nullptr ? p->load() : fallback;
        return gridOn ? grid.amountFor(row, panel, step) : panel;
    };

    sampleLayer.setHostBpm(transport.bpm);

    // PITCH +/-: two lanes, one transpose. Full on a lane is an octave (the prototype's
    // ±12 at level 3). They oppose rather than fight — paint both and they cancel, which
    // is the only reading that isn't arbitrary.
    {
        const float up   = amt(pitchUpParam,   Nebula2::GridRow::PitchUp,   0.0f);
        const float down = amt(pitchDownParam, Nebula2::GridRow::PitchDown, 0.0f);
        //
        // The NOTE LANE adds to this: it is a musical transpose in semitones on the step,
        // where Pitch +/- is a percentage of an octave. They sum, because a user who has
        // painted both plainly wants both — and because silently letting one win would be
        // a control that stops working depending on another one.
        const float noteSemis = (gridOn && step >= 0) ? (float) grid.getNote(step) : 0.0f;
        sampleLayer.setPitchOffsetSemitones((up - down) * 0.12f + noteSemis);
    }

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
                sampleLayer.render(sampleBus, cursor, pos - cursor);
                cursor = pos;
            }

            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                sampleLayer.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            }
            else if (msg.isController())
            {
                // Record only — see getMidiLearn(). Learning also happens here so the CC
                // that arms a control is the one that moves it.
                midiLearn.noteCC(msg.getControllerNumber(), msg.getControllerValue());
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
            sampleLayer.render(sampleBus, cursor, numSamples - cursor);
        }
    }

    // Layer buses -> main, through the mixer.
    //
    // SOLO wins over the levels: soloing the sample and then wondering why the kit is still
    // audible because its knob is up would be a control that doesn't do what it says.
    {
        // Input trim into the FX chain. This was a two-layer mixer until the drum synth was
        // removed; with one layer there is nothing to balance, so Drums and Solo went with
        // it rather than staying as controls that cannot do anything.
        const float smpLvl = smpVolParam != nullptr ? smpVolParam->load() / 100.0f : 1.0f;

        // Smoothed: a jump straight to a new gain clicks. Set up in prepareToPlay.
        smpGain.setTargetValue(smpLvl);

        for (int i = 0; i < numSamples; ++i)
        {
            const float sg = smpGain.getNextValue();
            for (int c = 0; c < numChannels && c < 2; ++c)
                buffer.addSample(c, i, sampleBus.getSample(c, i) * sg);
        }
    }

    // Colour: drive -> crush/width -> squeeze -> tone, on the summed layers.
    {
        Nebula2::ColourChain::Params cp;
        cp.drive     = amt(driveParam,   Nebula2::GridRow::Drive,   0.0f);
        cp.crush     = amt(crushParam,   Nebula2::GridRow::Crush,   0.0f);
        cp.squeeze   = amt(squeezeParam, Nebula2::GridRow::Squeeze, 0.0f);
        cp.tone      = amt(toneParam,    Nebula2::GridRow::Tone,    100.0f);
        cp.width     = amt(widthParam,   Nebula2::GridRow::Width,   100.0f);
        cp.driveChar = driveCharParam != nullptr ? (int) driveCharParam->load() : 0;
        cp.on        = fxOnParam      != nullptr ? fxOnParam->load() > 0.5f : true;
        // Pump is grid-sequenceable now, like the other Colour lanes.
        cp.pump      = amt(pumpParam, Nebula2::GridRow::Pump, 0.0f);
        cp.ppq       = transport.ppq;   // so the duck lands on the beat
        cp.bpm       = transport.bpm;
        cp.resonate  = amt(resonateParam, Nebula2::GridRow::Resonate, 0.0f);
        cp.resoKey   = resoKeyParam != nullptr ? (int) resoKeyParam->load() : 0;
        cp.resoScale = (Nebula2::ResoScale) juce::jlimit(
                           0, (int) Nebula2::ResoScale::Count - 1,
                           resoScaleParam != nullptr ? (int) resoScaleParam->load() : 0);
        colourChain.process(buffer, cp);
    }

    // REVERSE / STUTTER: per-step playback effects. They need history (you can't reverse
    // what hasn't played yet), so StepFx keeps a ring and reads back out of it. Both rest
    // at 0 = untouched. The step clock free-runs off the tempo so they work with the grid
    // off too, and re-syncs on a grid step so they land on the beat.
    {
        const float revAmt  = juce::jlimit(0.0f, 1.0f,
                                  amt(reverseParam, Nebula2::GridRow::Reverse, 0.0f) / 100.0f);
        const float stutAmt = juce::jlimit(0.0f, 1.0f,
                                  amt(stutterParam, Nebula2::GridRow::Stutter, 0.0f) / 100.0f);
        const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
        const float shatAmt = juce::jlimit(0.0f, 1.0f,
                                  amt(shatterParam, Nebula2::GridRow::Shatter, 0.0f) / 100.0f);
        const double stepLen = 0.25 * (60.0 / bpm) * getSampleRate();   // one 1/16 note
        stepFx.process(buffer, stepLen, step, revAmt, stutAmt, shatAmt);
    }

    // Morph: blend the four scenes at the pad's position and run the multi-effect.
    // Sits after Colour and before Space, so its filter/drive shape the coloured signal
    // and its `spc` can feed the sends.
    float morphSpaceSend = 0.0f;
    {
        const bool padOn = padOnParam != nullptr && padOnParam->load() > 0.5f;
        if (padOn)
        {
            const float baseX = padXParam != nullptr ? padXParam->load() : 0.5f;
            const float baseY = padYParam != nullptr ? padYParam->load() : 0.5f;

            // Auto-motion: the dot orbits the (padX,padY) centre, tempo-locked. Off => the
            // sliders own the position, exactly as before.
            const auto motion = (Nebula2::MorphMotion) (morphMotionParam != nullptr
                                    ? juce::jlimit(0, 4, (int) morphMotionParam->load()) : 0);
            float tx = baseX, ty = baseY;
            if (motion != Nebula2::MorphMotion::Off)
            {
                static constexpr double barsFor[] = { 1.0, 2.0, 4.0, 8.0 };
                const int rateIdx = morphRateParam != nullptr ? juce::jlimit(0, 3, (int) morphRateParam->load()) : 1;
                const double beatsPerCycle = barsFor[rateIdx] * 4.0;
                double phase = transport.ppq / beatsPerCycle;
                phase -= std::floor(phase);
                const float size = (morphSizeParam != nullptr ? morphSizeParam->load() : 40.0f) / 100.0f * 0.5f;
                float dx = 0.0f, dy = 0.0f;
                Nebula2::morphMotionOffset(motion, phase, size, dx, dy);
                tx = juce::jlimit(0.0f, 1.0f, baseX + dx);
                ty = juce::jlimit(0.0f, 1.0f, baseY + dy);
            }

            // Glide: smooth the effective position toward the target so motion (and sudden
            // slider jumps) don't zipper. 0 = instant.
            const float glide = (morphGlideParam != nullptr ? morphGlideParam->load() : 0.0f) / 100.0f;
            const float coeff = glide <= 0.0f ? 1.0f
                              : 1.0f - std::exp(-1.0f / juce::jmax(1.0f, glide * 0.05f * (float) getSampleRate()
                                                                   / (float) juce::jmax(1, numSamples)));
            morphEffX += (tx - morphEffX) * coeff;
            morphEffY += (ty - morphEffY) * coeff;
            morphDrawX.store(morphEffX);
            morphDrawY.store(morphEffY);   // so the UI dot follows the motion

            const auto scene = Nebula2::blendMorph(morphScenes, morphEffX, morphEffY);
            morph.process(buffer, scene, transport.bpm, true);
            morphSpaceSend = scene.spc;      // the pad's own space amount
        }
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

    // Haunt: a drone conjured from the loaded slices, ADDED before Space so the reverb and
    // delay swallow it (the prototype routes it into spaceIn). Off at 0.
    {
        // Haunt is grid-sequenceable: paint the lane to swell the drone in and out.
        const float hauntAmt = amt(hauntParam, Nebula2::GridRow::Haunt, 0.0f);
        sampleLayer.renderHaunt(buffer, 0, numSamples, hauntAmt);
    }

    // GATE: a hard per-step gate, driven by its grid lane. This is the lane you paint to
    // chop the beat into rhythmic holes. Neutral (unpainted) = 0 = no gating, so it costs
    // nothing until you use it. Smoothed so a gate edge doesn't click.
    {
        const float gateAmt = juce::jlimit(0.0f, 1.0f,
                                           amt(gateParam, Nebula2::GridRow::Gate, 0.0f) / 100.0f);
        const float target = 1.0f - gateAmt;          // 100% = fully closed
        const float coeff  = 1.0f - std::exp(-1.0f / (0.002f * (float) getSampleRate()));  // ~2ms
        for (int i = 0; i < numSamples; ++i)
        {
            gateGain += (target - gateGain) * coeff;
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * gateGain);
        }
    }

    // Space: parallel reverb + tempo-synced delay send (dry is preserved).
    {
        const int wantChar = revCharParam != nullptr ? (int) revCharParam->load() : 1;
        const float wantSize = revSizeParam != nullptr ? revSizeParam->load() : 50.0f;
        if (wantChar != wantedRevChar.load() || std::abs(wantSize - wantedRevSize.load()) > 0.5f)
        {
            wantedRevChar.store(wantChar);
            wantedRevSize.store(wantSize);
            triggerAsyncUpdate();          // rebuild the IR off the audio thread (allocates)
        }

        Nebula2::SpaceProcessor::Params sp;
        // The Morph pad's own space amount adds to the reverb send, so moving the dot into
        // a wet corner genuinely gets wetter (that's what `spc` means in a scene).
        sp.revMix   = juce::jlimit(0.0f, 100.0f,
                                   amt(revMixParam, Nebula2::GridRow::Reverb, 0.0f) + morphSpaceSend);
        sp.dlyMix   = amt(dlyMixParam, Nebula2::GridRow::Delay,  0.0f);
        sp.dlyFb    = dlyFbParam   != nullptr ? dlyFbParam->load()   : 40.0f;
        sp.dlySync  = (Nebula2::DelaySync) (dlySyncParam != nullptr ? juce::jlimit(0, 5, (int) dlySyncParam->load()) : 2);
        sp.mode     = (Nebula2::DelayMode) (dlyModeParam != nullptr ? juce::jlimit(0, 2, (int) dlyModeParam->load()) : 0);
        sp.on       = spaceOnParam != nullptr ? spaceOnParam->load() > 0.5f : true;
        sp.bpm      = transport.bpm;       // musical time, from the host
        space.process(buffer, sp);
    }

    // Modular rack: the whole mix goes in, whatever the patch does comes out. Sits last
    // before the master, exactly where the prototype tapped it (postGain -> rack -> master).
    //
    // The rack is skipped entirely when it's off OR when nothing is patched to the main
    // out — in both cases the dry beat passes untouched. A rack you haven't wired yet must
    // never silence your track.
    {
        const bool rackOn = rackOnParam == nullptr || rackOnParam->load() > 0.5f;
        if (rackOn)
        {
            // TRY the lock; never wait on it. If the editor is mid-patch, this block uses
            // last block's graph — one block of a stale cable is inaudible, whereas an
            // audio thread blocked on the message thread is a dropout.
            const juce::SpinLock::ScopedTryLockType sl(rackLock);
            if (sl.isLocked())
            {
                readRackDials();
                rackEngine.process(buffer, rackGraph, rackDials);
            }
        }
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
    // The arrangement (which slice each pad plays) is not derivable from the audio, so it
    // has to travel with the project or a shuffled break comes back in its original order.
    sampleNode.setProperty("sliceOrder", sampleLayer.sliceOrderToString(), nullptr);
    sampleNode.setProperty("sliceFx", sampleLayer.sliceSettingsToString(), nullptr);
    sampleNode.setProperty("ampShape", Nebula2::ampShapeToString(sampleLayer.getAmpShape()), nullptr);
    sampleNode.setProperty("ampOn", sampleLayer.isAmpShapeOn(), nullptr);

    // The grid is structured data, not a flat parameter — it needs saving explicitly too,
    // or a reopened project comes back with an empty pattern.
    auto gridNode = state.getOrCreateChildWithName("GRID", nullptr);
    gridNode.setProperty("cells", grid.toString(), nullptr);
    gridNode.setProperty("notes", grid.notesToString(), nullptr);

    // Morph scenes are structured data too (see MorphPad.h) — save them explicitly.
    auto morphNode = state.getOrCreateChildWithName("MORPH", nullptr);
    morphNode.setProperty("scenes", Nebula2::morphScenesToString(morphScenes), nullptr);

    // The patch IS the song. Save it, or a reopened project comes back with every rack dial
    // restored and not one cable — which would look right and sound like nothing.
    {
        const juce::SpinLock::ScopedLockType sl(rackLock);
        auto rackNode = state.getOrCreateChildWithName("RACK", nullptr);
        rackNode.setProperty("patch", rackGraph.toString(), nullptr);
    }

    auto uiNode = state.getOrCreateChildWithName("UI", nullptr);
    uiNode.setProperty("scale", uiScale, nullptr);
    uiNode.setProperty("gridDice", gridDiceDensity, nullptr);

    // The MIDI map is hardware setup, not a sound. It travels with the project so a
    // controller keeps working when you reopen it.
    uiNode.setProperty("midiMap", midiLearn.toString(), nullptr);

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
        const juce::String order = sampleNode.getProperty("sliceOrder").toString();
        const juce::String fx    = sampleNode.getProperty("sliceFx").toString();
        if (path.isNotEmpty() && path != sampleLayer.getSourcePath())
        {
            const juce::ScopedLock sl(pendingPathLock);
            pendingSamplePath = path;
            // Held until AFTER the load. loadFile re-slices and resets the order, so
            // applying it here would restore the arrangement and then immediately wipe it.
            pendingSliceOrder = order;
            pendingSliceFx = fx;
            // The envelope is not sample-dependent, so it can be applied immediately —
            // unlike the order and per-slice settings, which a re-slice would wipe.
            sampleLayer.setAmpShape(Nebula2::ampShapeFromString(sampleNode.getProperty("ampShape").toString()),
                                    (bool) sampleNode.getProperty("ampOn", false));
            triggerAsyncUpdate();
        }
        else
        {
            // Same sample already loaded (or none): nothing will re-slice, so apply now.
            sampleLayer.sliceOrderFromString(order);
            sampleLayer.sliceSettingsFromString(fx);
            sampleLayer.setAmpShape(Nebula2::ampShapeFromString(sampleNode.getProperty("ampShape").toString()),
                                    (bool) sampleNode.getProperty("ampOn", false));
        }
    }

    if (auto gridNode = tree.getChildWithName("GRID"); gridNode.isValid())
    {
        grid.fromString(gridNode.getProperty("cells").toString());
        grid.notesFromString(gridNode.getProperty("notes").toString());
    }

    if (auto morphNode = tree.getChildWithName("MORPH"); morphNode.isValid())
        morphScenes = Nebula2::morphScenesFromString(morphNode.getProperty("scenes").toString());

    if (auto rackNode = tree.getChildWithName("RACK"); rackNode.isValid())
    {
        auto restored = Nebula2::RackGraph::fromString(rackNode.getProperty("patch").toString());
        const juce::SpinLock::ScopedLockType sl(rackLock);
        rackGraph = restored;
    }

    if (auto uiNode = tree.getChildWithName("UI"); uiNode.isValid())
    {
        uiScale = (float) uiNode.getProperty("scale", 0.0);
        gridDiceDensity = juce::jlimit(0, 2, (int) uiNode.getProperty("gridDice", 1));
        midiLearn.fromString(uiNode.getProperty("midiMap").toString());
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

Nebula2::Snapshot Nebula2AudioProcessor::captureSnapshot() const
{
    Nebula2::Snapshot s;
    s.grid       = grid.toString();
    s.gridNotes  = grid.notesToString();
    s.sliceOrder = sampleLayer.sliceOrderToString();
    s.sliceFx    = sampleLayer.sliceSettingsToString();
    s.scenes     = Nebula2::morphScenesToString(morphScenes);
    {
        // The audio thread reads the patch, so take the same lock the editor does. This is
        // the message thread, where waiting is allowed.
        const juce::SpinLock::ScopedLockType sl(const_cast<juce::SpinLock&>(rackLock));
        s.rack = rackGraph.toString();
    }
    return s;
}

void Nebula2AudioProcessor::applySnapshot(const Nebula2::Snapshot& s)
{
    grid.fromString(s.grid);
    grid.notesFromString(s.gridNotes);
    sampleLayer.sliceOrderFromString(s.sliceOrder);
    sampleLayer.sliceSettingsFromString(s.sliceFx);
    morphScenes = Nebula2::morphScenesFromString(s.scenes);
    {
        auto restored = Nebula2::RackGraph::fromString(s.rack);
        const juce::SpinLock::ScopedLockType sl(rackLock);
        rackGraph = restored;
    }
}

bool Nebula2AudioProcessor::undoState()
{
    // `current` goes onto the redo stack, so undo is reversible rather than a one-way trip.
    Nebula2::Snapshot out;
    if (! history.undo(captureSnapshot(), out)) return false;
    applySnapshot(out);
    return true;
}

bool Nebula2AudioProcessor::redoState()
{
    Nebula2::Snapshot out;
    if (! history.redo(captureSnapshot(), out)) return false;
    applySnapshot(out);
    return true;
}

void Nebula2AudioProcessor::applyPendingMidi()
{
    // MESSAGE THREAD. setValueNotifyingHost notifies listeners, repaints attached controls
    // and in some hosts allocates — none of which belongs in processBlock, which is why the
    // audio thread only records and this drains.
    std::array<float, Nebula2::MidiLearn::numCCs> vals {};
    std::array<bool, Nebula2::MidiLearn::numCCs> has {};
    if (midiLearn.drainPending(vals, has) == 0) return;

    for (int cc = 0; cc < Nebula2::MidiLearn::numCCs; ++cc)
    {
        if (! has[(size_t) cc]) continue;
        const auto id = midiLearn.paramForCC(cc);
        if (id.isEmpty()) continue;                     // a CC nobody mapped
        if (auto* p = apvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(vals[(size_t) cc]);
            p->endChangeGesture();
        }
    }
}
