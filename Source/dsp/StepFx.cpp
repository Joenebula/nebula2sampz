#include "StepFx.h"

#include <cmath>

namespace Nebula2
{
    void StepFx::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        // Four seconds of history: comfortably more than one grid step even at 40 BPM
        // (a 1/16 there is 0.375 s), with room for the block on top. Allocated once.
        ringLen = juce::jmax(1024, (int) (spec.sampleRate * 4.0));
        ring.setSize((int) juce::jmax(2u, spec.numChannels), ringLen, false, true, true);
        reset();
    }

    void StepFx::reset() noexcept
    {
        ring.clear();
        writePos = 0;
        posInStep = 0.0;
        lastGridStep = -999;
        shatGain = 1.0f;
    }

    float StepFx::readRing(int chan, int offsetBack) const noexcept
    {
        if (ringLen <= 0) return 0.0f;
        int idx = writePos - offsetBack;
        idx %= ringLen;
        if (idx < 0) idx += ringLen;
        return ring.getSample(juce::jmin(chan, ring.getNumChannels() - 1), idx);
    }

    void StepFx::process(juce::AudioBuffer<float>& buffer, double stepLenSamples, int gridStep,
                         float reverseAmt, float stutterAmt, float shatterAmt) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || ringLen <= 0) return;

        const float rev  = juce::jlimit(0.0f, 1.0f, reverseAmt);
        const float stut = juce::jlimit(0.0f, 1.0f, stutterAmt);
        const float shat = juce::jlimit(0.0f, 1.0f, shatterAmt);

        // Keep the history current even when the effects are off — otherwise the first
        // step after you engage one would read silence.
        const bool active = (rev > 0.001f || stut > 0.001f);
        const float shatSmooth = 1.0f - std::exp(-1.0f / (0.002f * (float) sampleRate));  // ~2 ms

        const double stepLen = juce::jlimit(64.0, (double) ringLen - 4.0, stepLenSamples);
        const double chunk   = juce::jmax(32.0, stepLen / (double) stutterRepeats);

        // A grid step change re-syncs the clock so the effect lands on the beat.
        if (gridStep >= 0 && gridStep != lastGridStep)
        {
            lastGridStep = gridStep;
            posInStep = 0.0;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. Record what's coming in.
            for (int c = 0; c < numChannels && c < ring.getNumChannels(); ++c)
                ring.setSample(c, writePos, buffer.getSample(c, i));

            if (active)
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    const float dry = buffer.getSample(c, i);
                    float wet = dry;

                    if (rev > 0.001f)
                    {
                        // Read BACKWARDS from where this step began: at posInStep p we want
                        // the sample p before the step start, i.e. (p + p) back from now,
                        // because "now" is already p past the step start.
                        const int back = (int) (posInStep * 2.0) + 1;
                        wet = dry + (readRing(c, back) - dry) * rev;
                    }

                    if (stut > 0.001f)
                    {
                        // Repeat the chunk that ran just BEFORE this step: walk forward
                        // inside it, wrapping every `chunk` samples.
                        const double intoChunk = std::fmod(posInStep, chunk);
                        const int back = (int) (posInStep + chunk - intoChunk) + 1;
                        const float s = readRing(c, back);
                        wet = wet + (s - wet) * stut;
                    }

                    buffer.setSample(c, i, wet);
                }
            }

            // SHATTER: the tempo-locked gate, riding this same step clock.
            {
                const float target = shat > 0.001f
                                   ? shatterGainAt(posInStep / stepLen, shat) : 1.0f;
                shatGain += (target - shatGain) * shatSmooth;
                if (shat > 0.001f || shatGain < 0.9999f)
                    for (int c = 0; c < numChannels; ++c)
                        buffer.setSample(c, i, buffer.getSample(c, i) * shatGain);
            }

            if (++writePos >= ringLen) writePos = 0;
            posInStep += 1.0;
            if (posInStep >= stepLen) posInStep -= stepLen;
        }
    }
}
