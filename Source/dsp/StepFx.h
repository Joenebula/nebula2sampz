#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // The tempo-locked SHATTER gate shape: open for the first half of each step, ducked to
    // (1 - amount) for the second. One definition, used by both the Colour-block Shatter
    // and the Morph engine's per-scene shatter — law 8, never write the same helper twice.
    // `phase` is 0..1 within one step.
    inline float shatterGainAt(double phase, float amount) noexcept
    {
        return phase < 0.5 ? 1.0f : (1.0f - juce::jlimit(0.0f, 1.0f, amount));
    }

    // Per-step "playback" effects: REVERSE, STUTTER and SHATTER, the prototype's grid lanes.
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
        // reverseAmt / stutterAmt / shatterAmt: 0..1, already gated by the grid lane.
        // Shatter rides the same step clock (it IS a 1/16 gate), so it lives here rather
        // than growing a second clock somewhere else.
        void process(juce::AudioBuffer<float>& buffer, double stepLenSamples, int gridStep,
                     float reverseAmt, float stutterAmt, float shatterAmt) noexcept;

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
        float shatGain = 1.0f;       // smoothed, so a gate edge doesn't click
    };
}
