#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // The master bus tail: smoothed output gain -> brickwall limiter -> hard safety clamp.
    // Its own translation unit so it is unit-testable on synthetic buffers without the
    // whole plugin. Guarantee: no sample leaves process() above 0 dBFS, and output is
    // always finite (no NaN/inf), for any finite input.
    class MasterProcessor
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // Applies gainLinear (smoothed) then, if enabled, the limiter; always clamps to
        // [-1, 1] last. Processes in place. Real-time safe: no allocations, no locks.
        void process(juce::AudioBuffer<float>& buffer, float gainLinear, bool limiterEnabled) noexcept;

    private:
        juce::dsp::Limiter<float> limiter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
        double sampleRate = 44100.0;
    };
}
