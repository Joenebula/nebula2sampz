#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    enum class ReverbChar { Room = 0, Hall, Plate, Cathedral, Reverse };

    // Synthesises the prototype's reverb impulse response (noise * decay envelope, one-pole
    // darkened, character-dependent). Two changes from the browser build, both deliberate:
    //   * SEEDED rng -> the IR is reproducible, so presets and offline renders are
    //     deterministic (the prototype used Math.random(), which was not).
    //   * returned as a plain AudioBuffer for a juce::dsp::Convolution to load.
    // Length = max(64, sampleRate*seconds). Character envelopes:
    //   room (1-t)^5.5 · hall (1-t)^2.6 · plate (1-t)^1.9·shimmer · cathedral (1-t)^1.15
    //   · reverse = a decaying envelope time-reversed, so it swells into the hit (the
    //     prototype grew-then-reversed, cancelling to a decay — fixed here). preDelay:
    //     cathedral 45ms, hall 20ms.
    juce::AudioBuffer<float> makeImpulseResponse(double sampleRate, double seconds,
                                                 ReverbChar character, int seed = 20250715);

    // Convolution reverb: loads a synthesised IR into juce::dsp::Convolution and blends
    // dry/wet. IR loading is asynchronous (JUCE background thread) and swaps in on a later
    // process() call. Real-time safe: dry is kept in a preallocated scratch buffer.
    class Reverb
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // Rebuilds the IR for the character and hands it to the convolution engine.
        void setCharacter(ReverbChar character);

        // wetMix 0..1 dry/wet blend.
        void process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept;

        int getCurrentIRSize() const noexcept { return (int) conv.getCurrentIRSize(); }

    private:
        juce::dsp::Convolution conv;
        juce::AudioBuffer<float> dryScratch;
        double sampleRate = 44100.0;
        ReverbChar currentChar = ReverbChar::Hall;
    };
}
