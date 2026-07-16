#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MasterProcessor.h"
#include "Transport.h"
#include "dsp/DrumKit.h"
#include "dsp/ColourChain.h"
#include "dsp/SpaceProcessor.h"
#include "dsp/SampleLayer.h"

// MIDI-triggered drum voices -> Colour FX -> Space send -> master chain.
//
// Inherits AsyncUpdater for one specific reason: changing the reverb character rebuilds an
// impulse response, which allocates. processBlock can only NOTE that the character changed
// and kick an async update; the rebuild itself happens on the message thread.
class Nebula2AudioProcessor final : public juce::AudioProcessor,
                                    private juce::AsyncUpdater
{
public:
    Nebula2AudioProcessor();
    ~Nebula2AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    // The editor drives sample loading (message thread — decoding allocates).
    Nebula2::SampleLayer& getSampleLayer() noexcept { return sampleLayer; }

    // Most-recent host transport the audio thread saw (for the editor to display later).
    Nebula2::TransportState getTransport() const noexcept { return transport; }

private:
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Cached raw-parameter pointers for lock-free reads in processBlock.
    std::atomic<float>* masterParam { nullptr };
    std::atomic<float>* limiterParam { nullptr };
    std::atomic<float>* driveParam { nullptr };
    std::atomic<float>* driveCharParam { nullptr };
    std::atomic<float>* crushParam { nullptr };
    std::atomic<float>* squeezeParam { nullptr };
    std::atomic<float>* toneParam { nullptr };
    std::atomic<float>* widthParam { nullptr };
    std::atomic<float>* fxOnParam { nullptr };
    std::atomic<float>* revMixParam { nullptr };
    std::atomic<float>* revCharParam { nullptr };
    std::atomic<float>* dlyMixParam { nullptr };
    std::atomic<float>* dlyFbParam { nullptr };
    std::atomic<float>* dlySyncParam { nullptr };
    std::atomic<float>* spaceOnParam { nullptr };

    Nebula2::MasterProcessor masterProcessor;

    // Layer buses: sample-slicer layer and synth-drum layer, summed into the main output.
    // The drum layer is live (MIDI-triggered); the sample layer awaits the slicer.
    juce::AudioBuffer<float> sampleBus, drumBus;

    Nebula2::DrumKit drumKit;
    Nebula2::SampleLayer sampleLayer;
    Nebula2::ColourChain colourChain;
    Nebula2::SpaceProcessor space;

    // Reverb-character reload: the audio thread only sets `wantedRevChar` and pokes the
    // async updater; handleAsyncUpdate() does the (allocating) IR rebuild off-thread.
    std::atomic<int> wantedRevChar { 1 };   // 1 = Hall, matches the param default
    void handleAsyncUpdate() override;

    Nebula2::TransportState transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessor)
};
