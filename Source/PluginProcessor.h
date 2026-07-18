#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MasterProcessor.h"
#include "Transport.h"
#include "dsp/DrumKit.h"
#include "dsp/ColourChain.h"
#include "dsp/SpaceProcessor.h"
#include "dsp/SampleLayer.h"
#include "dsp/FxGrid.h"
#include "dsp/MorphEngine.h"
#include "dsp/RackGraph.h"
#include "dsp/RackModules.h"

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

    // Factory presets via the host's own preset menu (no bespoke browser to maintain).
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}   // factory presets are read-only

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    // The editor drives sample loading (message thread — decoding allocates).
    Nebula2::SampleLayer& getSampleLayer() noexcept { return sampleLayer; }

    // Slice-count choice index -> actual count (4/8/16/32/64).
    static int sliceCountFromChoice(int choiceIndex) noexcept;

    // Grid-steps choice index -> actual step count (8/16/32).
    static int gridStepsFromChoice(int choiceIndex) noexcept;

    // The FX grid. The editor paints cells on the message thread; the audio thread only
    // READS them. Cells are single bytes, so a read racing a paint yields either the old
    // or the new level for one block — inaudible, and far cheaper than locking the
    // audio thread.
    Nebula2::FxGrid& getGrid() noexcept { return grid; }

    // Morph scenes: message-thread only (the editor edits them; the audio thread reads a
    // blend each block). Floats, so a race yields old-or-new for one block — inaudible.
    std::array<Nebula2::MorphScene, 4>& getMorphScenes() noexcept { return morphScenes; }

    // The pad dot's effective position (base + auto-motion), so the UI dot follows the
    // motion rather than sitting where the sliders are. 0.5,0.5 when the pad's off.
    float getMorphDrawX() const noexcept { return morphDrawX.load(); }
    float getMorphDrawY() const noexcept { return morphDrawY.load(); }

    // The rack's patch. The editor edits it on the message thread; the audio thread reads
    // it each block. Guarded by a lock the AUDIO thread never waits on: it tries, and if
    // the editor happens to be mid-edit it reuses last block's patch. One block of a stale
    // cable is inaudible; a locked audio thread is a dropout.
    Nebula2::RackGraph& getRackGraph() noexcept { return rackGraph; }
    juce::SpinLock& getRackLock() noexcept { return rackLock; }

    // The LFO's current value (-1..1) so the rack UI can draw a moving picture.
    float getRackLfoValue() const noexcept { return rackEngine.lfoValue(); }

    // In-app audition: play the loaded break WITHOUT the DAW rolling. Toggled by the
    // editor's Play button. The moment the host transport starts, it takes over and this
    // clears itself — so the DAW is always the authority when it's running.
    void setAudition(bool on) noexcept { auditionActive.store(on); }
    bool isAuditioning() const noexcept { return auditionActive.load(); }

    // UI zoom. Lives here (not in the editor) so it survives the editor being closed and
    // reopened, and travels in the session. NOT a parameter: it's a property of the screen
    // you're looking at, not of the song — automating it would be nonsense.
    float getUiScale() const noexcept { return uiScale; }
    void  setUiScale(float s) noexcept { uiScale = s; }

    // Which step is currently sounding (for the UI playhead). -1 if the grid is off.
    int getCurrentGridStep() const noexcept { return currentGridStep.load(); }

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
    std::atomic<float>* pumpParam { nullptr };
    std::atomic<float>* gateParam { nullptr };
    float gateGain = 1.0f;      // audio thread: smoothed gate level
    std::atomic<float>* fxOnParam { nullptr };
    std::atomic<float>* revMixParam { nullptr };
    std::atomic<float>* revCharParam { nullptr };
    std::atomic<float>* revSizeParam { nullptr };
    std::atomic<float>* dlyMixParam { nullptr };
    std::atomic<float>* dlyFbParam { nullptr };
    std::atomic<float>* dlySyncParam { nullptr };
    std::atomic<float>* dlyModeParam { nullptr };
    std::atomic<float>* hauntParam { nullptr };
    std::atomic<float>* spaceOnParam { nullptr };
    std::atomic<float>* sliceModeParam { nullptr };
    std::atomic<float>* sliceCountParam { nullptr };
    std::atomic<float>* sensitivityParam { nullptr };
    std::atomic<float>* gridOnParam { nullptr };
    std::atomic<float>* gridStepsParam { nullptr };

    Nebula2::FxGrid grid;
    std::atomic<int> currentGridStep { -1 };

    std::atomic<float>* padOnParam { nullptr };
    std::atomic<float>* padXParam { nullptr };
    std::atomic<float>* padYParam { nullptr };
    std::atomic<float>* morphMotionParam { nullptr };
    std::atomic<float>* morphRateParam { nullptr };
    std::atomic<float>* morphSizeParam { nullptr };
    std::atomic<float>* morphGlideParam { nullptr };
    float morphEffX = 0.5f, morphEffY = 0.5f;          // audio thread: glided position
    std::atomic<float> morphDrawX { 0.5f }, morphDrawY { 0.5f };   // for the UI dot
    Nebula2::MorphEngine morph;
    std::array<Nebula2::MorphScene, 4> morphScenes = Nebula2::defaultMorphScenes();

    // --- Modular rack ---
    Nebula2::RackGraph  rackGraph;
    Nebula2::RackEngine rackEngine;
    juce::SpinLock      rackLock;
    Nebula2::RackDials  rackDials;            // audio thread only; refilled from params each block
    std::atomic<float>* rackOnParam { nullptr };
    std::array<std::atomic<float>*, 33> rackDialParams {};   // cached, in readRackDials() order
    void readRackDials() noexcept;

    Nebula2::MasterProcessor masterProcessor;

    // Layer buses: sample-slicer layer and synth-drum layer, summed into the main output.
    // The drum layer is live (MIDI-triggered); the sample layer awaits the slicer.
    juce::AudioBuffer<float> sampleBus, drumBus;

    Nebula2::DrumKit drumKit;
    Nebula2::SampleLayer sampleLayer;
    Nebula2::ColourChain colourChain;
    Nebula2::SpaceProcessor space;

    // Off-thread work triggered from processBlock. Both the reverb IR rebuild and
    // re-slicing ALLOCATE, so the audio thread only records what's wanted and pokes the
    // async updater; handleAsyncUpdate() does the real work on the message thread.
    std::atomic<int> wantedRevChar { 1 };        // 1 = Hall, matches the param default
    std::atomic<float> wantedRevSize { 50.0f };  // reverb Size %, matches the param default
    std::atomic<int> wantedSliceMode { 0 };      // 0 = Grid
    std::atomic<int> wantedSliceCount { 16 };
    std::atomic<float> wantedSensitivity { 0.5f };

    // Sample restore from saved state (decoding allocates; setStateInformation may not be
    // on the message thread, so the load is deferred to handleAsyncUpdate).
    juce::CriticalSection pendingPathLock;
    juce::String pendingSamplePath;

    int currentProgram = 0;
    float uiScale = 0.0f;      // 0 = never chosen; the editor then follows the screen

    // In-app audition (see setAudition). auditionActive is set from the editor; the rest is
    // audio-thread only. B4 (83) is the whole-break note.
    std::atomic<bool> auditionActive { false };
    double auditionPpq = 0.0;          // audio thread: the synthesized transport position
    double lastHostPpq = -1.0;         // audio thread: to detect the host actually rolling
    bool   auditionWasRolling = false;
    static constexpr int wholeBreakNote = 83;

    void handleAsyncUpdate() override;

    Nebula2::TransportState transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessor)
};
