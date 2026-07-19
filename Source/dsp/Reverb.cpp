#include "Reverb.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    juce::AudioBuffer<float> makeImpulseResponse(double sampleRate, double seconds,
                                                 ReverbChar character, int seed)
    {
        const int len = Reverb::irLengthFor(sampleRate, seconds);
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

        if (rateChanged || needsBigger || preparedBlock == 0 || conv == nullptr)
        {
            // Part 2: REPLACE the engine rather than re-prepare it.
            //
            // The crash is Convolution::prepare() draining its BackgroundMessageQueue at the
            // same time as its own loader thread drains it: one reads a slot the other has
            // already taken, gets an empty FixedSizeFunction, calls it, and throws
            // std::bad_function_call. The only way to be certain no load is in flight when
            // prepare runs is to run it on an engine nobody has ever queued a load on.
            //
            // A brand new Convolution qualifies by construction. No polling, no timeout, no
            // window - the queue prepare() drains is provably empty because the object was
            // born three lines ago. Destroying the old one joins its loader thread first.
            //
            // This replaces waitForIrIdle(), which could not have worked: it polled
            // getCurrentIRSize() for a queued load to land, but JUCE only swaps a loaded IR
            // in during process(), so on the message thread with no audio running that size
            // never changes. It would have spun to its 200ms timeout and continued anyway.
            // (It never even got that far - nothing ever set the size it waited for, so it
            // returned instantly. Two independent reasons the same function did nothing.)
            //
            // Cost: an allocation and a thread join, on the message thread, only when the
            // rate genuinely changes or the block outgrows a 2048-sample ceiling. That is
            // rare, and it is the correct place to pay for correctness.
            conv = std::make_unique<juce::dsp::Convolution>();

            auto s = spec;
            s.maximumBlockSize = (juce::uint32) wantBlock;
            conv->prepare(s);

            preparedRate  = sampleRate;
            preparedBlock = wantBlock;

            // A new engine has no IR at all, so one must be loaded whatever the rate did.
            irDirty = true;
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
        if (! juce::approximatelyEqual(sampleRate, irSampleRate) || getCurrentIRSize() <= 1)
        {
            irSampleRate = sampleRate;
            irDirty = true;
        }
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
        // DELIBERATELY DOES NOT CALL conv->reset(). This is the actual crash fix, and it is
        // the first one in this file derived from JUCE's concurrency contract rather than
        // from a stack trace.
        //
        // Convolution's background queue documents its own rule:
        //     "This function is only safe to call from a single thread at a time."  (push)
        // popMutex guards the two CONSUMERS. Nothing guards the PRODUCERS, and there are
        // two of them:
        //     Pimpl::reset()          -> destroyPreviousEngine() -> push   [MESSAGE thread]
        //     Pimpl::processSamples() -> postPendingCommand()    -> push   [AUDIO thread]
        //                             -> destroyPreviousEngine() -> push
        // The queue sits on an AbstractFifo, which is single-producer. Two concurrent
        // writers corrupt the write index; a reader then pops a slot nobody wrote, finds a
        // default-constructed FixedSizeFunction, calls it, and throws std::bad_function_call.
        // That is the exception we have been chasing, and every recorded crash site
        // (Restoring default layout, Non-releasing audio processing, Automation) is a test
        // that calls prepareToPlay while audio is running - exactly the overlap needed.
        //
        // Reverb::reset() is called from prepareToPlay, so it WAS the message-thread
        // producer. Removing the call removes that side of the race: the audio thread is
        // then the only thread that ever pushes, which is the contract the queue asks for.
        //
        // What it costs: the convolution tail is not explicitly flushed here. In practice
        // nothing is lost - prepare() already installs a fresh engine and clears
        // previousEngine, and a rate change now replaces the whole Convolution anyway, so
        // the state this call would have cleared does not survive either path.
        //
        // NOT CLAIMED AS PROVEN. This is a mechanism-level argument, not a measurement.
        // The pluginval campaign is the evidence; see docs/VST_WORKFLOW.md.
    }

    void Reverb::setCharacter(ReverbChar character, double sizeSeconds)
    {
        currentChar = character;
        currentSize = juce::jlimit(0.25, 6.75, sizeSeconds);

        if (conv == nullptr) return;   // not prepared yet; prepare() will load it

        conv->loadImpulseResponse(makeImpulseResponse(sampleRate, currentSize, character),
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
        // No engine yet = no reverb, and crucially NOT silence: the dry blend below still
        // runs, so an unprepared reverb passes the beat rather than muting the track.
        if (conv != nullptr) conv->process(ctx);
        else                 buffer.clear();   // wet is nothing; the dry blend restores it

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
