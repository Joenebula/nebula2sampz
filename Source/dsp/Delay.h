#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // The prototype's delay, ported faithfully — including its stereo topology, which the
    // first port simplified away:
    //   * the SEND feeds the LEFT line only; the right line is fed purely by cross-feedback,
    //     so the echoes bounce L -> R (a true ping-pong that starts on one side);
    //   * the two taps are hard-panned (+/-0.75), giving the wide bounce;
    //   * the damping lowpass sits ONLY on the L->R feedback path (the R->L path is raw).
    //
    // Three modes (the prototype's PONG / DUB / WARP):
    //   PingPong — symmetric-ish, 5.2 kHz damping.
    //   Dub      — right time halved, right feedback x0.4, 900 Hz damping (dark, dubby).
    //   Warp     — like Dub's timing, 5.2 kHz damping, PLUS a slow LFO (0.28 Hz) on the left
    //              delay time: tape wow/flutter and pitch drift.
    enum class DelayMode { PingPong = 0, Dub, Warp };

    class PingPongDelay
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // timeSeconds: base delay time (musical, from host BPM). feedback 0..~0.92.
        // wetMix 0..1 — the added (parallel-send) level, dry is preserved by the caller.
        void process(juce::AudioBuffer<float>& buffer,
                     float timeSeconds, float feedback, float wetMix, DelayMode mode) noexcept;

    private:
        // Channel 0 = the "left" line, channel 1 = the "right" line.
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 1 << 18 };
        juce::dsp::IIR::Filter<float> damp;        // on the L->R feedback path only
        double sampleRate = 44100.0;
        double warpPhase = 0.0;
        int    lastDampFreq = 0;                   // so coefficients rebuild only on change
    };
}
