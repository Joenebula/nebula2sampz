#include "Reverb.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    juce::AudioBuffer<float> makeImpulseResponse(double sampleRate, double seconds,
                                                 ReverbChar character, int seed)
    {
        const int len = juce::jmax(64, (int) (sampleRate * seconds));
        juce::AudioBuffer<float> ir(2, len);

        const int preDelay = character == ReverbChar::Cathedral ? (int) (sampleRate * 0.045)
                           : character == ReverbChar::Hall      ? (int) (sampleRate * 0.020)
                                                                : 0;

        juce::Random rng((juce::int64) seed);   // seeded -> reproducible

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = ir.getWritePointer(ch);
            float lp = 0.0f;

            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / (float) len;
                if (i < preDelay) { d[i] = 0.0f; continue; }

                float env;
                switch (character)
                {
                    case ReverbChar::Plate:     env = std::pow(1.0f - t, 1.9f) * (1.0f + 0.35f * std::sin(t * 180.0f + (float) ch)); break;
                    case ReverbChar::Room:      env = std::pow(1.0f - t, 5.5f);  break;
                    case ReverbChar::Cathedral: env = std::pow(1.0f - t, 1.15f); break;
                    // Reverse = a decaying (hall-like) envelope, time-reversed below, so it
                    // genuinely SWELLS into the hit. The prototype computed a *growing*
                    // envelope then reversed it, which cancels to a plain decay (the
                    // "reverse" never actually reversed) — corrected here to match intent.
                    case ReverbChar::Reverse:   env = std::pow(1.0f - t, 2.6f);  break;
                    case ReverbChar::Hall:
                    default:                    env = std::pow(1.0f - t, 2.6f);  break;
                }

                const float v = (rng.nextFloat() * 2.0f - 1.0f) * env;
                lp += (v - lp) * (character == ReverbChar::Cathedral ? 0.22f : 0.55f);
                d[i] = character == ReverbChar::Plate ? v : lp * 1.6f;
            }

            if (character == ReverbChar::Reverse)
                std::reverse(d, d + len);
        }

        return ir;
    }

    void Reverb::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        dryScratch.setSize((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        // THE CRASH FIX. Everything below is about not letting Convolution::prepare run
        // while its background thread is draining the same single-consumer queue.
        //
        // Part 1: don't re-prepare unless the convolution genuinely needs it.
        //
        // Convolution::prepare cares about the sample rate and the maximum block size. A
        // host walking block sizes at a fixed rate — which is what pluginval's automation
        // tests do, and what a DAW does when you change buffer size — used to re-prepare
        // every time. Preparing at a generous ceiling instead means a smaller block never
        // re-prepares at all, so the overlap simply cannot arise for the common case.
        const int wantBlock = juce::jmax((int) spec.maximumBlockSize, preparedBlockFloor);
        const bool rateChanged  = ! juce::approximatelyEqual(sampleRate, preparedRate);
        const bool needsBigger  = wantBlock > preparedBlock;

        if (rateChanged || needsBigger || preparedBlock == 0)
        {
            // Part 2: for the rare case that DOES re-prepare, make sure no load is in
            // flight first. A rate change is the realistic trigger, and two in quick
            // succession is the only way to get here with the background thread busy.
            waitForIrIdle();

            auto s = spec;
            s.maximumBlockSize = (juce::uint32) wantBlock;
            conv.prepare(s);

            preparedRate  = sampleRate;
            preparedBlock = wantBlock;
        }

        // The IR load is deliberately NOT done here — see reloadIrIfNeeded().
        //
        // This line used to be setCharacter(currentChar, currentSize), and it was the
        // crash. The chain:
        //   Convolution::prepare()    drains its BackgroundMessageQueue (popAll)
        //   loadImpulseResponse()     queues work and wakes the background thread
        // so every prepare left work IN FLIGHT for the next prepare to race. That queue is
        // single-producer / single-CONSUMER, and prepare's popAll plus the background
        // thread's popAll are two consumers: one reads a slot the other already took, gets
        // an empty FixedSizeFunction, calls it, and throws std::bad_function_call.
        //
        // pluginval's Automation test calls prepareToPlay repeatedly with changing block
        // sizes, which is why it surfaced there and at roughly 1 run in 18 — it needs the
        // background thread to still be busy when the next prepare lands.
        // ...and only mark it stale when the IR could ACTUALLY be different.
        //
        // The impulse response is generated at the sample rate; the block size has nothing
        // to do with it. Marking dirty on every prepare meant every prepare queued a load,
        // so a host walking block sizes at a fixed rate (which is exactly what pluginval's
        // Automation tests do) kept the background thread permanently busy and left the
        // race wide open. Deferring the load out of prepare's call stack narrowed the
        // window; not queueing a pointless load closes most of what was left.
        if (! juce::approximatelyEqual(sampleRate, irSampleRate) || conv.getCurrentIRSize() <= 1)
        {
            irSampleRate = sampleRate;
            irDirty = true;
        }
    }

    void Reverb::waitForIrIdle() noexcept
    {
        // MESSAGE THREAD ONLY. Waits for a queued IR load to land before we touch
        // Convolution::prepare.
        //
        // JUCE exposes no "is the loader idle" flag, so the observable signal is the IR
        // size changing to the one we asked for. A load that happens to produce the SAME
        // size is indistinguishable from one still pending — hence the hard timeout, which
        // is the important part: worst case we wait 200 ms and proceed exactly as before,
        // which is the current behaviour. Blocking the message thread forever would be far
        // worse than the crash we are fixing.
        if (expectedIrSize <= 0) return;

        const auto deadline = juce::Time::getMillisecondCounter() + 200;
        while ((int) conv.getCurrentIRSize() != expectedIrSize
               && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep(1);

        expectedIrSize = 0;
    }

    void Reverb::reloadIrIfNeeded()
    {
        // MESSAGE THREAD, and deliberately in a SEPARATE call stack from prepare(). That
        // separation is the fix: the queue is empty when Convolution::prepare drains it, so
        // its popAll and the background thread's are never both live.
        if (! irDirty) return;
        irDirty = false;
        setCharacter(currentChar, currentSize);
    }

    void Reverb::reset()
    {
        conv.reset();
    }

    void Reverb::setCharacter(ReverbChar character, double sizeSeconds)
    {
        currentChar = character;
        currentSize = juce::jlimit(0.25, 6.75, sizeSeconds);
        conv.loadImpulseResponse(makeImpulseResponse(sampleRate, currentSize, character),
                                 sampleRate,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::yes);
    }

    void Reverb::process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const float mix = juce::jlimit(0.0f, 1.0f, wetMix);

        // Keep the dry signal (preallocated scratch — no allocation in the callback).
        for (int c = 0; c < numChannels && c < dryScratch.getNumChannels(); ++c)
            dryScratch.copyFrom(c, 0, buffer, c, 0, numSamples);

        // Wet = convolution.
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        // Blend out = dry*(1-mix) + wet*mix.
        for (int c = 0; c < numChannels && c < dryScratch.getNumChannels(); ++c)
        {
            auto* out = buffer.getWritePointer(c);
            const auto* dry = dryScratch.getReadPointer(c);
            for (int i = 0; i < numSamples; ++i)
                out[i] = dry[i] * (1.0f - mix) + out[i] * mix;
        }
    }
}
