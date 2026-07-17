#include "Delay.h"

#include <cmath>

namespace Nebula2
{
    void PingPongDelay::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec stereo = spec;
        stereo.numChannels = 2;
        delay.prepare(stereo);
        delay.setMaximumDelayInSamples(juce::jmax(64, (int) (sampleRate * 4.0)));

        juce::dsp::ProcessSpec mono = spec;
        mono.numChannels = 1;
        damp.prepare(mono);
        // Prime the coefficient storage so the first in-place assign doesn't allocate.
        *damp.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(sampleRate, 5200.0f, 0.707f);
        lastDampFreq = 5200;

        reset();
    }

    void PingPongDelay::reset()
    {
        delay.reset();
        damp.reset();
        warpPhase = 0.0;
    }

    void PingPongDelay::process(juce::AudioBuffer<float>& buffer,
                                float timeSeconds, float feedback, float wetMix, DelayMode mode) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        const float fb  = juce::jlimit(0.0f, 0.95f, feedback);
        const float mix = juce::jlimit(0.0f, 1.0f, wetMix);

        // Per-mode settings, straight from the prototype's tuneSpace.
        const float rightTimeMul = (mode == DelayMode::PingPong) ? 1.0f : 0.5f;
        const float rightFbMul   = (mode == DelayMode::PingPong) ? 1.0f : 0.4f;
        const int   dampFreq     = (mode == DelayMode::Dub) ? 900 : 5200;
        const float warpDepth    = (mode == DelayMode::Warp) ? 0.0032f : 0.0f;   // seconds

        // Damping coefficients: rebuild only when the mode's frequency changed (in place,
        // no allocation), so per-block cost is nil in a steady mode.
        if (dampFreq != lastDampFreq)
        {
            *damp.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
                                     sampleRate, (float) dampFreq, 0.707f);
            lastDampFreq = dampFreq;
        }

        const float baseL = juce::jlimit(1.0f, (float) (sampleRate * 4.0), timeSeconds * (float) sampleRate);
        const float delR  = juce::jlimit(1.0f, (float) (sampleRate * 4.0),
                                         timeSeconds * rightTimeMul * (float) sampleRate);

        // The two hard-panned taps (+/-0.75), equal-power. dL sits left, dR sits right.
        //   pan -0.75 -> L 0.981, R 0.195 ;  pan +0.75 -> L 0.195, R 0.981
        constexpr float pL_l = 0.981f, pL_r = 0.195f;   // left tap -> (L, R)
        constexpr float pR_l = 0.195f, pR_r = 0.981f;   // right tap -> (L, R)

        const double warpInc = 0.28 / sampleRate;       // 0.28 Hz LFO

        float* L = buffer.getWritePointer(0);
        float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = L[i];
            const float inR = R != nullptr ? R[i] : inL;
            const float inMono = (inL + inR) * 0.5f;   // the send sums into the left line

            // Warp modulates the LEFT delay time (tape wow). 0 when not in Warp mode.
            const float warp = warpDepth * std::sin((float) warpPhase * juce::MathConstants<float>::twoPi);
            const float delL = juce::jlimit(1.0f, (float) (sampleRate * 4.0),
                                            baseL + warp * (float) sampleRate);

            const float dL = delay.popSample(0, delL);
            const float dR = delay.popSample(1, delR);

            // Feedback wiring, exactly the prototype's:
            //   right line  <- damped( fb * left out )     [L->R, filtered]
            //   left line   <- send + (fb*rightFbMul) * right out   [R->L, raw]
            delay.pushSample(1, damp.processSample(fb * dL));
            delay.pushSample(0, inMono + (fb * rightFbMul) * dR);

            // Advance the two read heads (DelayLine needs a push per channel per sample).
            // (popSample above already read; pushSample sets the next write.)

            const float wetL = dL * pL_l + dR * pR_l;
            const float wetR = dL * pL_r + dR * pR_r;

            L[i] = inL + wetL * mix;
            if (R != nullptr) R[i] = inR + wetR * mix;

            warpPhase += warpInc;
            if (warpPhase >= 1.0) warpPhase -= 1.0;
        }
    }
}
