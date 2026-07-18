#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace Nebula2
{
    // The maths behind the EQ's draggable curve, kept OUT of the component so it can be
    // tested without a window. The component then owns only mouse plumbing and painting.
    //
    // Everything here is pure: same inputs, same answer, no state.

    // --- frequency <-> x, logarithmically ---
    //
    // Log, not linear. An EQ display spans 20 Hz to 20 kHz - three decades - and on a linear
    // axis the bottom two of those decades share the first 5% of the width. Every kick and
    // bass decision would happen in a few pixels.
    inline float eqFreqToNorm (float hz) noexcept
    {
        const float f = juce::jlimit (20.0f, 20000.0f, hz);
        return std::log (f / 20.0f) / std::log (1000.0f);      // 20 * 1000 = 20000
    }

    inline float eqNormToFreq (float t) noexcept
    {
        return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, t));
    }

    // --- gain <-> y ---
    // y grows downward, so +18 dB is at the TOP (norm 0) and -18 dB at the bottom.
    inline float eqGainToNorm (float dB, float maxDb = 18.0f) noexcept
    {
        return 0.5f - juce::jlimit (-maxDb, maxDb, dB) / (2.0f * maxDb);
    }

    inline float eqNormToGain (float t, float maxDb = 18.0f) noexcept
    {
        return (0.5f - juce::jlimit (0.0f, 1.0f, t)) * 2.0f * maxDb;
    }

    // --- one band's magnitude response, in dB, at a given frequency ---
    //
    // These are the ANALYTIC responses of the same RSBQ shapes the DSP builds, so the drawn
    // curve is the filter rather than a decorative bump that happens to sit nearby. If the
    // picture and the sound disagree, the picture is a lie and the EQ is unusable.
    //
    // type: 0 = peak, 1 = low shelf, 2 = high shelf.
    inline float eqBandResponseDb (int type, float centreHz, float q, float gainDb,
                                   float atHz) noexcept
    {
        if (std::abs (gainDb) < 0.001f) return 0.0f;

        const float f0 = juce::jmax (1.0f, centreHz);
        const float f  = juce::jmax (1.0f, atHz);
        const float qq = juce::jmax (0.05f, q);

        if (type == 0)
        {
            // Peak: full gain at f0, falling away either side at a rate set by Q. Written
            // in octaves from the centre so the shape is symmetric on the log axis the
            // display actually uses.
            const float oct = std::log2 (f / f0);
            const float bw  = 1.0f / qq;                 // ~octaves to the -3dB point
            const float x   = oct / juce::jmax (0.01f, bw);
            return gainDb / (1.0f + x * x * 4.0f);
        }

        // Shelves: full gain in the passband, zero beyond the corner, with the transition
        // width set by Q. tanh gives the S-curve without needing the biquad's complex maths.
        const float oct = std::log2 (f / f0);
        const float k   = juce::jmax (0.2f, qq) * 1.6f;
        const float s   = std::tanh (oct * k);           // -1 below the corner, +1 above
        return type == 1 ? gainDb * (0.5f - 0.5f * s)    // low shelf: lifts BELOW f0
                         : gainDb * (0.5f + 0.5f * s);   // high shelf: lifts ABOVE f0
    }

    // Hz formatted the way an EQ labels it: "180 Hz", "1.6 kHz". Shown live while dragging,
    // which is the whole point of a parametric band - you need to know where you put it.
    inline juce::String eqFormatHz (float hz)
    {
        if (hz >= 1000.0f)
        {
            const float k = hz / 1000.0f;
            return juce::String (k, k < 10.0f ? 2 : 1) + " kHz";
        }
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }
}
