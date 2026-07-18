#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // Per-step "playback" effects: REVERSE and STUTTER, the prototype's grid lanes.
    //
    // Both need HISTORY — you cannot reverse audio you haven't heard yet, and a stutter
    // repeats something already played. So this keeps a preallocated ring of recent audio
    // and reads out of it:
    //
    //   Reverse — plays the step's worth of audio that just went past, backwards.
    //   Stutter — grabs the last short chunk and repeats it for the rest of the step.
    //
    // Both are a DRY->WET blend, so the lane's cell level (or the panel knob) scales how
    // much of the effect you get. At 0 the input passes through untouched.
    //
    // The step clock free-runs off the tempo, so these work with the grid OFF too (the
    // panel knob alone). When the grid IS running, a step change re-syncs the clock so the
    // effect lands on the beat rather than drifting.
    class StepFx
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset() noexcept;

        // stepLenSamples: length of one grid step (a 1/16 note) at the current tempo.
        // gridStep: the grid's current step index, or -1 when the grid is off.
        // reverseAmt / stutterAmt: 0..1, already gated by the grid lane.
        void process(juce::AudioBuffer<float>& buffer, double stepLenSamples, int gridStep,
                     float reverseAmt, float stutterAmt) noexcept;

        // How many times the captured chunk repeats inside one step.
        static constexpr int stutterRepeats = 4;

    private:
        float readRing(int chan, int offsetBack) const noexcept;

        juce::AudioBuffer<float> ring;
        int ringLen = 0;
        int writePos = 0;
        double posInStep = 0.0;      // samples since this step began
        int lastGridStep = -999;
        double sampleRate = 44100.0;
    };
}
