#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Nebula2
{
    // A bool parameter whose value is actually a BOOL.
    //
    // juce::AudioParameterBool::setValue(0.577f) stores 0.577f verbatim, and getValue()
    // hands it straight back — so a "bool" can sit at 0.577 forever. get() reads it as
    // true, and the APVTS tree stores the snapped 0/1, so the DSP never sees the oddity.
    // But state restoration does: the tree round-trips 1.0 -> 1.0, no change is detected,
    // no listener fires, and the parameter keeps its stale 0.577. pluginval at strictness
    // 10 catches exactly this, at random, on a random subset of the bools — which is why
    // it "passed first try" once and fails 1-3 tests per run now. That first pass was luck.
    //
    // Snapping on the way in makes the value honest and the round-trip exact. No real host
    // sends 0.577 to a bool, so this changes nothing audible — it removes a way for the
    // plugin to be subtly wrong rather than fixing a wrongness you could hear.
    // getValue() is the only thing that leaks the un-snapped float, so snap THERE. No
    // re-entrancy, no shadow state, no fighting the base class: get(), the APVTS tree and
    // every listener already read through this. Two lines, and the value a bool reports is
    // a bool.
    struct SnappedBool final : juce::AudioParameterBool
    {
        using juce::AudioParameterBool::AudioParameterBool;
        // get() is the public read of the very same field, and already does the >= 0.5
        // comparison. (getValue/setValue are both private in the base, so this is also the
        // only route that doesn't fight the class.)
        float getValue() const override { return get() ? 1.0f : 0.0f; }
    };

    // Builds the full parameter layout for the plugin's AudioProcessorValueTreeState.
    // Kept in its own translation unit so it can be unit-tested without instantiating
    // the whole processor (see Tests/).
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // The layer mixer's gain law, in ONE place because processBlock and its test both need
    // it — a copy in each is a test that passes while the audio does something else, which
    // is the single most common way a green suite has lied in this project.
    //
    // SOLO beats the level knobs deliberately: soloing the sample and still hearing the kit
    // because its fader happened to be up would be a control that doesn't do what it says.
    // solo: 0 = off, 1 = sample, 2 = drums.
    inline void layerMixGains(int solo, float smpPercent, float drmPercent,
                              float& smpGain, float& drmGain) noexcept
    {
        smpGain = (solo == 2) ? 0.0f : smpPercent / 100.0f;
        drmGain = (solo == 1) ? 0.0f : drmPercent / 100.0f;
    }
}
