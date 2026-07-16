#pragma once

#include <vector>

namespace Nebula2
{
    // Slice-boundary maths ported from the prototype. All pure functions over a mono
    // channel, so they're testable without an audio device or a loaded file.

    // Nudge an index to the nearest zero crossing within `radius` (falls back to the
    // quietest nearby sample). Slicing on a zero crossing is what stops chops clicking.
    int snapZero(const float* data, int numSamples, int index, int radius);

    // Even grid slicing: numSlices+1 boundaries, first at 0 and last at numSamples,
    // interior ones snapped to zero crossings and forced strictly increasing.
    std::vector<int> computeGridSlices(const float* data, int numSamples, int numSlices, int snapRadius);

    // Transient slicing: spectral-flux onset detection (RMS envelope -> positive flux ->
    // adaptive threshold -> peak picking with a minimum gap), each onset zero-snapped.
    // sensitivity 0..1 (higher = more slices). Always returns 0 first and numSamples last.
    std::vector<int> detectTransients(const float* data, int numSamples, double sampleRate, float sensitivity);

    // The loop's own tempo, implied by its length: (bars*4) / durationSeconds * 60.
    double derivedBpm(int bars, int numSamples, double sampleRate);

    // Playback rate to move a loop from its native tempo to the host tempo.
    double tempoRatio(double playBpm, double nativeBpm);
}
