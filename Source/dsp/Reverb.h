#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

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
}
