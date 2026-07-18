#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace Nebula2
{
    // RESONATE — a bank of high-Q bandpass filters tuned to a musical scale, fed in
    // PARALLEL with the dry signal. Percussion excites them, so a kick rings out a bass
    // note and a hat shimmers: the break turns tonal.
    //
    // Ported from the prototype's buildResonators(): eight voices, Q 42, root A1 (55 Hz),
    // per-voice gain 0.9/8, output amount * 1.6. The prototype taps the signal AFTER the
    // tone filter and sums the bank straight to master ("excite the resonators
    // post-filter"), which is where ColourChain calls this from.
    //
    // Real-time safe: the filters are prepared once and retuned in place with
    // ArrayCoefficients (the make*() factories allocate — they return *new Coefficients).
    enum class ResoScale { Minor = 0, Major, Phrygian, Fifths, Count };

    const char* resoScaleName(ResoScale s) noexcept;

    // The eight scale degrees, in semitones above the root. The prototype's tables.
    const std::array<int, 8>& resoScaleDegrees(ResoScale s) noexcept;

    // Voice frequency in Hz: root A1 = 55 Hz, transposed by key (semitones above A) and
    // the scale degree. Free function so the tuning can be tested without a filter bank.
    inline double resoVoiceHz(int keySemis, int degreeSemis) noexcept
    {
        return 55.0 * std::pow(2.0, (double) (keySemis + degreeSemis) / 12.0);
    }

    class Resonator
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr float voiceQ = 42.0f;      // high Q => it RINGS

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset() noexcept;

        // Retunes only when key or scale actually change, so this is free to call per
        // block. Allocation-free.
        void setTuning(int keySemis, ResoScale scale) noexcept;

        // Adds the bank's output into `buffer`, excited by `buffer`'s current contents.
        // amount is 0..1 (the panel's 0..100 %, /100). At 0 it is a no-op and the filter
        // state is left alone, so switching it on doesn't ring out stale history.
        void processAdd(juce::AudioBuffer<float>& buffer, float amount) noexcept;

    private:
        void retune() noexcept;

        std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, numVoices> filters;
        double sampleRate = 44100.0;
        int key = 0;
        ResoScale scale = ResoScale::Minor;
        bool tuningDirty = true;
        float outGain = 0.0f;          // smoothed, so the amount knob doesn't step
        bool wasSilent = true;
    };
}
