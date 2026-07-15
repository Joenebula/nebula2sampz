#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MasterProcessor.h"
#include "Transport.h"

// Phase 2: audio-engine skeleton. Real-time-safe callback, sample + drum layer buses,
// host transport read, master chain (gain -> limiter -> clamp). No sources/effects yet,
// so the output is still silent — Phase 3 fills the buses with sound.
class Nebula2AudioProcessor final : public juce::AudioProcessor
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

    // Most-recent host transport the audio thread saw (for the editor to display later).
    Nebula2::TransportState getTransport() const noexcept { return transport; }

private:
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Cached raw-parameter pointers for lock-free reads in processBlock.
    std::atomic<float>* masterParam { nullptr };
    std::atomic<float>* limiterParam { nullptr };

    Nebula2::MasterProcessor masterProcessor;

    // Layer buses: sample-slicer layer and synth-drum layer, summed into the main output.
    // Silent until Phase 3 adds sources; the routing skeleton lives here now.
    juce::AudioBuffer<float> sampleBus, drumBus;

    Nebula2::TransportState transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessor)
};
