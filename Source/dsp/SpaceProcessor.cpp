#include "SpaceProcessor.h"

namespace Nebula2
{
    double delaySyncInBeats(DelaySync s)
    {
        switch (s)
        {
            case DelaySync::Sixteenth:      return 0.25;
            case DelaySync::EighthTriplet:  return 1.0 / 3.0;
            case DelaySync::Eighth:         return 0.5;
            case DelaySync::DottedEighth:   return 0.75;
            case DelaySync::Quarter:        return 1.0;
            case DelaySync::DottedQuarter:  return 1.5;
        }
        return 0.5;
    }

    double delayTimeSeconds(DelaySync s, double bpm)
    {
        if (bpm <= 0.0) bpm = 120.0;
        return (60.0 / bpm) * delaySyncInBeats(s);
    }

    void SpaceProcessor::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reverb.prepare(spec);          // already loads an IR for the reverb's own character
        delay.prepare(spec);
        wetScratch.setSize((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        // Only load again if our wanted char/size actually differs from what reverb.prepare
        // already loaded. Route through setCharacterAndSize so its skip-guard applies — a
        // redundant load queues a second async IR swap (an allocation on JUCE's loader
        // thread, and it resets the convolution mid-flight, cutting the tail).
        setCharacterAndSize(character, sizePercent);
    }

    void SpaceProcessor::reset()
    {
        reverb.reset();
        delay.reset();
    }

    void SpaceProcessor::setCharacterAndSize(ReverbChar c, float newSizePercent)
    {
        // No redundant IR swap: rebuild only when the character or the size actually moved.
        const bool sameChar = (c == character && reverb.getCharacter() == c);
        const bool sameSize = std::abs(newSizePercent - sizePercent) < 0.5f;
        if (sameChar && sameSize) return;
        character   = c;
        sizePercent = newSizePercent;
        reverb.setCharacter(c, sizeSecondsFor(sizePercent));   // allocates — message thread only
    }

    void SpaceProcessor::process(juce::AudioBuffer<float>& buffer, const Params& p) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), wetScratch.getNumChannels());
        if (numSamples > wetScratch.getNumSamples()) return;   // never overrun the scratch

        const float revMix = p.on ? juce::jlimit(0.0f, 1.0f, p.revMix / 100.0f) : 0.0f;
        const float dlyMix = p.on ? juce::jlimit(0.0f, 1.0f, p.dlyMix / 100.0f) : 0.0f;

        // Zero means zero: with both sends down, Space costs nothing and adds nothing.
        if (revMix <= 0.0f && dlyMix <= 0.0f) return;

        // --- delay send (in place on a wet copy, dry preserved) ---
        if (dlyMix > 0.0f)
        {
            for (int c = 0; c < numChannels; ++c)
                wetScratch.copyFrom(c, 0, buffer, c, 0, numSamples);

            juce::AudioBuffer<float> view(wetScratch.getArrayOfWritePointers(), numChannels, numSamples);
            // wetMix 1 inside the delay: we do our own send-level blend below.
            delay.process(view, (float) delayTimeSeconds(p.dlySync, p.bpm),
                          juce::jlimit(0.0f, 0.92f, p.dlyFb / 100.0f), 1.0f, p.mode);

            // `view` now holds dry+echoes. (wet - dry) isolates the echo content, so the
            // send adds only the echoes and leaves the dry signal untouched.
            for (int c = 0; c < numChannels; ++c)
            {
                auto* out = buffer.getWritePointer(c);
                const auto* wet = wetScratch.getReadPointer(c);
                // x1.1 on the delay send, matching the prototype's dlyWet gain. Without it
                // the echoes sat ~0.8 dB under the prototype at the same Mix.
                for (int i = 0; i < numSamples; ++i)
                    out[i] += (wet[i] - out[i]) * dlyMix * 1.1f;
            }
        }

        // --- reverb send ---
        if (revMix > 0.0f)
        {
            for (int c = 0; c < numChannels; ++c)
                wetScratch.copyFrom(c, 0, buffer, c, 0, numSamples);

            juce::AudioBuffer<float> view(wetScratch.getArrayOfWritePointers(), numChannels, numSamples);
            reverb.process(view, 1.0f);   // fully wet; blended as a send below

            for (int c = 0; c < numChannels; ++c)
            {
                auto* out = buffer.getWritePointer(c);
                const auto* wet = wetScratch.getReadPointer(c);
                // x1.35 on the reverb send, matching the prototype's revWet gain — it sat
                // ~2.6 dB under the prototype at the same Mix without it.
                for (int i = 0; i < numSamples; ++i)
                    out[i] += wet[i] * revMix * 1.35f;
            }
        }
    }
}
