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

        // Loads the impulse response if prepare() marked it stale. MESSAGE THREAD, and it
        // must NOT be called from inside prepare — see the note there.
        void reloadIrIfNeeded();
        void reset();

        // Rebuilds the IR for the character AND size, and hands it to the convolution
        // engine. Allocates — message thread only. Each call queues an async IR load, and a
        // load landing RESETS the convolution state (killing any ringing tail), so never
        // call it redundantly (SpaceProcessor only calls it when char or size changed).
        // sizeSeconds is the tail length: the prototype's Size knob spanned 0.25..~6.75 s.
        void setCharacter(ReverbChar character, double sizeSeconds);
        ReverbChar getCharacter() const noexcept { return currentChar; }
        double getSizeSeconds() const noexcept { return currentSize; }

        // wetMix 0..1 dry/wet blend.
        void process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept;

        int getCurrentIRSize() const noexcept { return (int) conv.getCurrentIRSize(); }

    private:
        juce::dsp::Convolution conv;
        juce::AudioBuffer<float> dryScratch;
        double sampleRate = 44100.0;
        bool irDirty = true;   // prepare() sets this; reloadIrIfNeeded() clears it
        ReverbChar currentChar = ReverbChar::Hall;
        double currentSize = 2.0;      // seconds; the prototype's default Size (~50%)
    };
}
