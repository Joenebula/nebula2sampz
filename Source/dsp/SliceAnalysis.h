#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace Nebula2
{
    // What a slice sounds like. Enough to arrange a break musically rather than at random:
    // a kick wants the downbeat, hats want the gaps, and a tonal slice wants to be left
    // alone rather than chopped into a roll.
    enum class SliceKind { Perc = 0, Kick, Snare, Hat, Tonal, Count };

    const char* sliceKindName(SliceKind k) noexcept;

    struct SliceInfo
    {
        SliceKind kind = SliceKind::Perc;
        float rms = 0.0f;
        float brightHz = 0.0f;   // zero-crossing rate as a rough brightness
        float decay = 0.0f;      // tail energy / head energy: high = sustained
        float lowRatio = 0.0f;   // low-band energy vs overall: high = kick
    };

    // Classify one slice. Pure and free-standing so the thresholds can be tested against
    // synthesised kicks and hats without loading a file — a classifier nobody has checked
    // against known input is a guess with a confident name.
    //
    // Ported from the prototype's analyseSlices(), including its thresholds.
    SliceInfo analyseSlice(const float* samples, int numSamples, double sampleRate) noexcept;

    // Classify every slice of a break. `sliceStarts` is numSlices + 1 boundaries.
    std::vector<SliceInfo> analyseSlices(const juce::AudioBuffer<float>& audio,
                                         const std::vector<int>& sliceStarts,
                                         double sampleRate);

    // A musical arrangement of `count` slices, given what each one is.
    //
    // The point of the analysis: a plain shuffle scatters the kick anywhere, which mostly
    // sounds like a mistake. This puts something kick-like on the downbeats, keeps tonal
    // slices off the fast subdivisions, and lets hats and percussion fill the rest. Falls
    // back to a plain shuffle when nothing is known about the slices, so it degrades to the
    // honest thing rather than pretending to be clever.
    //
    // RNG by reference so a seed reproduces an arrangement.
    std::vector<int> musicalSliceOrder(const std::vector<SliceInfo>& info, int count,
                                       juce::Random& rng);
}
