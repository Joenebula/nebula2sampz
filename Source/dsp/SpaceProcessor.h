#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Reverb.h"
#include "Delay.h"

namespace Nebula2
{
    // Note divisions, in beats — the prototype's SYNC map. Delay time is ALWAYS derived
    // from host BPM * a division; milliseconds are never stored (law: musical time, so a
    // patch written at 120 still works at 140 and the echo follows the tempo).
    enum class DelaySync { Sixteenth = 0, EighthTriplet, Eighth, DottedEighth, Quarter, DottedQuarter };
    double delaySyncInBeats(DelaySync s);
    double delayTimeSeconds(DelaySync s, double bpm);

    // The prototype's "Space": reverb + tempo-synced ping-pong delay, running as a parallel
    // SEND (dry stays intact; wet is added), which is how it was wired in the browser build.
    //
    // Changing the reverb character rebuilds an impulse response — that allocates and must
    // NEVER happen on the audio thread. process() only reports that a rebuild is wanted;
    // the host-side owner does it via requestCharacter()/applyPendingCharacter() on the
    // message thread.
    class SpaceProcessor
    {
    public:
        struct Params
        {
            float revMix = 0.0f;    // 0..100 %
            float dlyMix = 0.0f;    // 0..100 %
            float dlyFb = 40.0f;    // 0..92 %
            DelaySync dlySync = DelaySync::Eighth;
            bool pingPong = true;
            double bpm = 120.0;
            bool on = true;
        };

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // Message thread only (allocates: synthesises + loads a new IR). Character and Size
        // both drive the IR, so they rebuild through one path — a redundant rebuild resets
        // the convolution and cuts the tail.
        void setCharacterAndSize(ReverbChar c, float sizePercent);
        ReverbChar getCharacter() const noexcept { return character; }
        float      getSizePercent() const noexcept { return sizePercent; }

        // sizePercent 0..100 -> seconds, the prototype's own curve (0.25 + (p/100)^1.9*6.5,
        // ~0.25..6.75 s). Public so the processor can decide whether a rebuild is needed.
        static double sizeSecondsFor(float sizePercent) noexcept
        {
            const double p = juce::jlimit(0.0, 1.0, (double) sizePercent / 100.0);
            return 0.25 + std::pow(p, 1.9) * 6.5;
        }

        void process(juce::AudioBuffer<float>& buffer, const Params& p) noexcept;

        int reverbIRSize() const noexcept { return reverb.getCurrentIRSize(); }

    private:
        Reverb reverb;
        PingPongDelay delay;
        juce::AudioBuffer<float> wetScratch;   // preallocated: the send's wet copy
        ReverbChar character = ReverbChar::Hall;
        float sizePercent = 50.0f;             // the prototype's default Size
        double sampleRate = 44100.0;
    };
}
