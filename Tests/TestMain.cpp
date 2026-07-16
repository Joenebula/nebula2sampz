// AUTO-gate tests, run via ctest in CI. Returns non-zero on any failure.
//   Phase 1 — parameter round-trip + automation write/read (dummy AudioProcessor).
//   Phase 2 — master chain (silence, no-clip, finite) + transport parse.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../Source/Parameters.h"
#include "../Source/ParameterIDs.h"
#include "../Source/MasterProcessor.h"
#include "../Source/Transport.h"
#include "../Source/dsp/ParametricEq.h"
#include "../Source/dsp/Saturator.h"
#include "../Source/dsp/Colour.h"
#include "../Source/dsp/Reverb.h"
#include "../Source/dsp/Delay.h"

#include <iostream>
#include <cmath>

using namespace juce;

struct DummyProcessor final : public AudioProcessor
{
    DummyProcessor()
        : AudioProcessor(BusesProperties().withOutput("Out", AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "PARAMS", Nebula2::createParameterLayout()) {}

    const String getName() const override { return "Dummy"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}

    void getStateInformation(MemoryBlock& mb) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary(*xml, mb);
    }
    void setStateInformation(const void* d, int s) override
    {
        if (auto xml = getXmlFromBinary(d, s))
            if (xml->hasTagName(apvts.state.getType()))
                apvts.replaceState(ValueTree::fromXml(*xml));
    }

    AudioProcessorValueTreeState apvts;
};

static int failures = 0;

static void check(bool cond, const String& msg)
{
    if (cond) std::cout << "ok:   " << msg << std::endl;
    else    { std::cerr << "FAIL: " << msg << std::endl; ++failures; }
}

int main()
{
    ScopedJuceInitialiser_GUI juceInit;

    DummyProcessor a;

    // 1. Layout: the seeded params exist.
    check(a.apvts.getParameter(Nebula2::ParamID::master)  != nullptr, "param 'master' exists");
    check(a.apvts.getParameter(Nebula2::ParamID::fltCut)  != nullptr, "param 'flt.cut' exists");
    check(a.apvts.getParameter(Nebula2::ParamID::revChar) != nullptr, "param 'revChar' exists");
    check(a.apvts.getParameter(Nebula2::ParamID::limiter) != nullptr, "param 'limiterOn' exists");

    auto* master  = a.apvts.getParameter(Nebula2::ParamID::master);
    auto* cutoff  = a.apvts.getParameter(Nebula2::ParamID::fltCut);
    auto* limiter = a.apvts.getParameter(Nebula2::ParamID::limiter);
    auto* revChar = a.apvts.getParameter(Nebula2::ParamID::revChar);

    // 2. Automation write/read: set a normalised value, read it straight back.
    master->setValueNotifyingHost(0.25f);
    limiter->setValueNotifyingHost(0.0f);                        // -> false
    cutoff->setValueNotifyingHost(0.8f);
    revChar->setValueNotifyingHost(revChar->convertTo0to1(3.0f)); // -> "Cathedral"

    // APVTS keeps its state tree and parameters in sync synchronously for these ops —
    // no message-loop pump needed.
    check(std::abs(master->getValue() - 0.25f) < 1.0e-4f, "master automation read-back");
    check(limiter->getValue() < 0.5f, "limiter automation read-back == false");

    // 3. State round-trip: save a -> load into b -> b's re-save byte-equals a's save.
    MemoryBlock stateA;
    a.getStateInformation(stateA);

    DummyProcessor b;
    b.setStateInformation(stateA.getData(), (int) stateA.getSize());

    MemoryBlock stateB;
    b.getStateInformation(stateB);
    check(stateA == stateB, "state round-trip: a.save() == b.load().save()");

    // 4. The reloaded values landed on b's live parameters.
    check(std::abs(b.apvts.getParameter(Nebula2::ParamID::master)->getValue() - 0.25f) < 1.0e-4f,
          "master value survives save/load");
    check(b.apvts.getParameter(Nebula2::ParamID::limiter)->getValue() < 0.5f,
          "limiter value survives save/load");

    // ---------------------------------------------------------------------------------
    // Phase 2 — master chain
    // ---------------------------------------------------------------------------------
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = 44100.0;
        spec.maximumBlockSize = 512;
        spec.numChannels      = 2;

        Nebula2::MasterProcessor mp;
        mp.prepare(spec);

        // Silence in -> silence out.
        AudioBuffer<float> silent(2, 512);
        silent.clear();
        mp.process(silent, 0.9f, true);
        check(silent.getMagnitude(0, 512) == 0.0f, "master: silence stays silent");

        // A signal well over 0 dBFS must come out clamped to [-1, 1] and finite.
        Nebula2::MasterProcessor mp2;
        mp2.prepare(spec);
        AudioBuffer<float> loud(2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                loud.setSample(c, i, 2.0f * std::sin(0.05f * (float) i));
        mp2.process(loud, 1.0f, true);
        const float mag = loud.getMagnitude(0, 512);
        check(std::isfinite(mag), "master: output is finite (no NaN/inf)");
        check(mag <= 1.0f + 1.0e-4f, "master: output never exceeds 0 dBFS");

        // Gain 0 mutes: over a long buffer the smoothed gain settles to silence.
        Nebula2::MasterProcessor mp3;
        mp3.prepare(spec);
        AudioBuffer<float> tone(1, 8192);
        for (int i = 0; i < 8192; ++i) tone.setSample(0, i, 0.5f);
        mp3.process(tone, 0.0f, false);
        float tail = 0.0f;
        for (int i = 8092; i < 8192; ++i) tail = std::max(tail, std::abs(tone.getSample(0, i)));
        check(tail < 1.0e-3f, "master: gain 0 mutes the signal");
    }

    // ---------------------------------------------------------------------------------
    // Phase 2 — transport parse
    // ---------------------------------------------------------------------------------
    {
        AudioPlayHead::PositionInfo pos;
        pos.setBpm(Optional<double>(140.0));
        pos.setPpqPosition(Optional<double>(4.0));
        pos.setIsPlaying(true);

        const auto t = Nebula2::readTransport(pos);
        check(std::abs(t.bpm - 140.0) < 1.0e-9, "transport: bpm read from host");
        check(std::abs(t.ppq - 4.0) < 1.0e-9, "transport: ppq read from host");
        check(t.playing, "transport: playing flag read from host");

        // Missing fields fall back to sane defaults, not garbage.
        AudioPlayHead::PositionInfo empty;
        const auto d = Nebula2::readTransport(empty);
        check(std::abs(d.bpm - 120.0) < 1.0e-9, "transport: default bpm when host omits it");
        check(!d.playing, "transport: not playing by default");
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — parametric EQ (RBJ magnitude response)
    // ---------------------------------------------------------------------------------
    {
        const double sr = 48000.0;
        const auto dB = [](double linear) { return juce::Decibels::gainToDecibels((float) linear); };

        // A +6 dB peak at 1 kHz: ~+6 dB at 1 kHz, ~flat two decades away.
        Nebula2::EqBandSettings peak;
        peak.on = true; peak.type = Nebula2::EqBandType::Peak;
        peak.freq = 1000.0f; peak.gainDb = 6.0f; peak.q = 1.0f;
        auto cp = Nebula2::makeBandCoefficients(peak, sr);
        check(std::abs(dB(cp->getMagnitudeForFrequency(1000.0, sr)) - 6.0f) < 0.5f,
              "EQ: +6 dB peak lifts 1 kHz by ~6 dB");
        check(std::abs(dB(cp->getMagnitudeForFrequency(20.0, sr))) < 0.5f,
              "EQ: peak leaves 20 Hz flat");

        // An off band is transparent (unity magnitude) everywhere it's asked.
        Nebula2::EqBandSettings off = peak; off.on = false;
        auto co = Nebula2::makeBandCoefficients(off, sr);
        check(std::abs(co->getMagnitudeForFrequency(1000.0, sr) - 1.0) < 1.0e-3,
              "EQ: disabled band is unity magnitude");

        // A high-pass at 500 Hz cuts DC hard and passes 10 kHz near unity.
        Nebula2::EqBandSettings hp;
        hp.on = true; hp.type = Nebula2::EqBandType::HighPass; hp.freq = 500.0f; hp.q = 0.707f;
        auto ch = Nebula2::makeBandCoefficients(hp, sr);
        check(dB(ch->getMagnitudeForFrequency(50.0, sr)) < -12.0f, "EQ: high-pass attenuates 50 Hz");
        check(std::abs(dB(ch->getMagnitudeForFrequency(10000.0, sr))) < 0.5f, "EQ: high-pass passes 10 kHz");

        // Processing a signal stays finite (no NaN/inf) and doesn't blow up.
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sr; spec.maximumBlockSize = 512; spec.numChannels = 2;
        Nebula2::ParametricEq eq;
        eq.prepare(spec);
        eq.setBand(0, peak);
        eq.setBand(1, hp);
        AudioBuffer<float> buf(2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample(c, i, 0.5f * std::sin(0.03f * (float) i));
        eq.process(buf);
        check(std::isfinite(buf.getMagnitude(0, 512)), "EQ: output finite after processing");
        check(buf.getMagnitude(0, 512) < 8.0f, "EQ: output bounded (no runaway)");
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — saturator (drive curves, crush, width)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // Drive transfer curve: pass-through at 0, bounded to [-1,1], monotonic on tube.
        check(std::abs(driveCurveSample(0.5f, 0.0f, DriveChar::Tube) - 0.5f) < 1.0e-6f,
              "drive: amt 0 is pass-through");
        for (auto ch : { DriveChar::Tube, DriveChar::Fuzz, DriveChar::Fold })
        {
            bool bounded = true, monotonicUp = true;
            float prev = driveCurveSample(-1.0f, 1.0f, ch);
            for (int i = -100; i <= 100; ++i)
            {
                const float x = (float) i / 100.0f;
                const float y = driveCurveSample(x, 1.0f, ch);
                if (! std::isfinite(y) || std::abs(y) > 1.0001f) bounded = false;
                if (ch == DriveChar::Tube && y < prev - 1.0e-4f) monotonicUp = false;
                prev = y;
            }
            check(bounded, "drive: output bounded to [-1,1] and finite");
            if (ch == DriveChar::Tube) check(monotonicUp, "drive(tube): monotonic increasing");
        }
        // Tube at full drive still maps the extremes to the rails.
        check(std::abs(driveCurveSample(1.0f, 1.0f, DriveChar::Tube) - 1.0f) < 1.0e-3f,
              "drive(tube): x=1 -> 1");

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0; spec.maximumBlockSize = 512; spec.numChannels = 2;

        // Crush: a constant 0.4 quantised to 3 bits then reconstruction-LP settles to a
        // quantised level (round(0.4 * 4) / 4 = 0.5). Proves quantiser + LP together.
        {
            Saturator sat;
            sat.prepare(spec);
            float tail = 0.0f;
            for (int blk = 0; blk < 8; ++blk)
            {
                AudioBuffer<float> buf(2, 512);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i) buf.setSample(c, i, 0.4f);
                sat.process(buf, 0.0f, DriveChar::Tube, 1.0f, 1.0f);
                if (blk == 7) tail = buf.getSample(0, 511);
            }
            check(std::abs(tail - 0.5f) < 0.02f, "crush: DC 0.4 -> quantised 0.5 after recon LP");
        }

        // Crush off + drive off + width 1 = exact pass-through.
        {
            Saturator sat;
            sat.prepare(spec);
            AudioBuffer<float> buf(2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) buf.setSample(c, i, 0.3f * std::sin(0.02f * (float) i));
            AudioBuffer<float> ref(buf);
            sat.process(buf, 0.0f, DriveChar::Tube, 0.0f, 1.0f);
            float maxDiff = 0.0f;
            for (int i = 0; i < 512; ++i) maxDiff = std::max(maxDiff, std::abs(buf.getSample(0, i) - ref.getSample(0, i)));
            check(maxDiff < 1.0e-6f, "saturator: fully neutral settings are bit-exact pass-through");
        }

        // Width: 0 collapses to mono (L==R), 2 widens (|L-R| grows).
        {
            Saturator sat;
            sat.prepare(spec);
            AudioBuffer<float> mono(2, 512);
            for (int i = 0; i < 512; ++i) { mono.setSample(0, i, 0.5f); mono.setSample(1, i, -0.2f); }
            AudioBuffer<float> wide(mono);

            sat.process(mono, 0.0f, DriveChar::Tube, 0.0f, 0.0f);
            check(std::abs(mono.getSample(0, 100) - mono.getSample(1, 100)) < 1.0e-5f,
                  "width 0: collapses to mono (L==R)");

            Saturator sat2; sat2.prepare(spec);
            sat2.process(wide, 0.0f, DriveChar::Tube, 0.0f, 2.0f);
            const float before = std::abs(0.5f - (-0.2f));
            const float after  = std::abs(wide.getSample(0, 100) - wide.getSample(1, 100));
            check(after > before, "width 2: widens the stereo difference");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — compressor (squeeze) + tone
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;
        const double sr = 48000.0;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sr; spec.maximumBlockSize = 512; spec.numChannels = 2;

        auto rmsCh0 = [](const AudioBuffer<float>& b)
        {
            double s = 0.0; const int n = b.getNumSamples();
            for (int i = 0; i < n; ++i) { const float v = b.getSample(0, i); s += (double) v * v; }
            return std::sqrt(s / (double) n);
        };

        // Drive a steady 440 Hz sine at 0.5 through the compressor for several blocks,
        // measure the settled output level. ratio 1 (squeeze 0) ~= unity; heavy squeeze
        // pulls it down (gain reduction).
        auto settledRms = [&](float squeeze)
        {
            Compressor c; c.prepare(spec);
            int phase = 0; double rms = 0.0;
            for (int blk = 0; blk < 10; ++blk)
            {
                AudioBuffer<float> b(2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) phase / (float) sr);
                    b.setSample(0, i, v); b.setSample(1, i, v); ++phase;
                }
                c.process(b, squeeze);
                if (blk == 9) rms = rmsCh0(b);
            }
            return rms;
        };

        const double rmsNoComp = settledRms(0.0f);
        const double rmsComp   = settledRms(1.0f);
        check(std::abs(rmsNoComp - 0.5 / std::sqrt(2.0)) < 0.05, "comp: squeeze 0 (ratio 1) is ~unity gain");
        check(rmsComp < rmsNoComp * 0.8, "comp: squeeze 1 applies gain reduction");
        check(std::isfinite(rmsComp), "comp: output finite");

        // Tone: fully closed is a hard lowpass (cuts 5 kHz); fully open is ~flat.
        const auto dB = [](double m) { return juce::Decibels::gainToDecibels((float) m); };
        auto lo = ToneFilter::coefficientsFor(0.0f, sr);
        auto hi = ToneFilter::coefficientsFor(1.0f, sr);
        check(dB(lo->getMagnitudeForFrequency(5000.0, sr)) < -12.0f, "tone 0: lowpass cuts 5 kHz");
        check(std::abs(dB(hi->getMagnitudeForFrequency(1000.0, sr))) < 1.0f, "tone 1: ~flat at 1 kHz");

        ToneFilter tf; tf.prepare(spec);
        AudioBuffer<float> tb(2, 512);
        for (int i = 0; i < 512; ++i) { const float v = 0.4f * std::sin(0.05f * (float) i); tb.setSample(0, i, v); tb.setSample(1, i, v); }
        tf.process(tb, 0.5f);
        check(std::isfinite(tb.getMagnitude(0, 512)), "tone: output finite after processing");
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — Space: reverb IR synthesis + ping-pong delay
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        auto windowRms = [](const AudioBuffer<float>& b, int start, int n)
        {
            double s = 0.0;
            for (int i = start; i < start + n; ++i) { const float v = b.getSample(0, i); s += (double) v * v; }
            return std::sqrt(s / (double) n);
        };

        // Reverb IR: length, determinism, decay shape.
        auto ir1 = makeImpulseResponse(48000.0, 1.0, ReverbChar::Hall, 42);
        auto ir2 = makeImpulseResponse(48000.0, 1.0, ReverbChar::Hall, 42);
        check(ir1.getNumSamples() == 48000, "reverb IR: length = sampleRate * seconds");
        bool identical = true;
        for (int i = 0; i < ir1.getNumSamples() && identical; i += 97)
            if (ir1.getSample(0, i) != ir2.getSample(0, i)) identical = false;
        check(identical, "reverb IR: seeded -> reproducible");
        check(std::isfinite(ir1.getMagnitude(0, ir1.getNumSamples())), "reverb IR: finite");
        check(windowRms(ir1, 2000, 2000) > windowRms(ir1, 40000, 2000), "reverb IR (hall): decays over time");

        auto irRev = makeImpulseResponse(48000.0, 1.0, ReverbChar::Reverse, 42);
        check(windowRms(irRev, 40000, 2000) > windowRms(irRev, 2000, 2000), "reverb IR (reverse): swells into the hit");

        auto irCath = makeImpulseResponse(48000.0, 1.0, ReverbChar::Cathedral, 42);
        check(std::abs(irCath.getSample(0, 100)) < 1.0e-9f, "reverb IR (cathedral): pre-delay silence at start");

        // Ping-pong delay.
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0; spec.maximumBlockSize = 1024; spec.numChannels = 2;

        // wetMix 0 = dry pass-through.
        {
            PingPongDelay d; d.prepare(spec);
            AudioBuffer<float> b(2, 1024);
            for (int i = 0; i < 1024; ++i) { const float v = 0.4f * std::sin(0.02f * (float) i); b.setSample(0, i, v); b.setSample(1, i, v); }
            AudioBuffer<float> ref(b);
            d.process(b, 100.0f / 48000.0f, 0.5f, 0.0f, true);
            float maxDiff = 0.0f;
            for (int i = 0; i < 1024; ++i) maxDiff = std::max(maxDiff, std::abs(b.getSample(0, i) - ref.getSample(0, i)));
            check(maxDiff < 1.0e-6f, "delay: wetMix 0 is dry pass-through");
        }

        // A single echo lands one delay-time later.
        {
            PingPongDelay d; d.prepare(spec);
            AudioBuffer<float> b(2, 1024); b.clear();
            b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f);   // impulse
            d.process(b, 100.0f / 48000.0f, 0.0f, 1.0f, false); // 100-sample delay, no feedback
            check(std::abs(b.getSample(0, 0) - 1.0f) < 1.0e-4f, "delay: dry impulse present at t=0");
            check(std::abs(b.getSample(0, 50)) < 1.0e-4f, "delay: silence before the echo");
            check(b.getSample(0, 100) > 0.5f, "delay: echo lands at the delay time");
        }

        // Ping-pong cross-feed leaks an L-only input into R; straight feedback does not.
        auto rChannelEnergy = [&](bool pingPong)
        {
            PingPongDelay d; d.prepare(spec);
            AudioBuffer<float> b(2, 1024); b.clear();
            b.setSample(0, 0, 1.0f);   // impulse in L only
            d.process(b, 100.0f / 48000.0f, 0.6f, 1.0f, pingPong);
            double s = 0.0; for (int i = 0; i < 1024; ++i) { const float v = b.getSample(1, i); s += (double) v * v; }
            return std::sqrt(s / 1024.0);
        };
        const double rPing = rChannelEnergy(true);
        const double rStraight = rChannelEnergy(false);
        // Straight feedback leaves R silent (== 0); ping-pong leaks L into R. Test the
        // routing by relative dominance, not an absolute floor (the damping LP makes the
        // single-impulse cross-feed small but non-zero).
        check(rStraight < 1.0e-6, "delay straight: L-only input stays out of R");
        check(rPing > 1.0e-4 && rPing > rStraight * 20.0, "delay ping-pong: L-only cross-feeds into R");

        // Convolution reverb engine. (Qualified: juce also has a juce::Reverb, and this
        // scope has `using namespace juce`, so the bare name would be ambiguous.)
        Nebula2::Reverb rev;
        rev.prepare(spec);
        rev.setCharacter(ReverbChar::Hall);

        // wetMix 0 is an exact dry pass-through (independent of async IR load).
        {
            AudioBuffer<float> b(2, 512);
            for (int i = 0; i < 512; ++i) { const float v = 0.4f * std::sin(0.02f * (float) i); b.setSample(0, i, v); b.setSample(1, i, v); }
            AudioBuffer<float> ref(b);
            rev.process(b, 0.0f);
            float maxDiff = 0.0f;
            for (int i = 0; i < 512; ++i) maxDiff = std::max(maxDiff, std::abs(b.getSample(0, i) - ref.getSample(0, i)));
            check(maxDiff < 1.0e-6f, "reverb engine: wetMix 0 is dry pass-through");
        }

        // Wait for the async IR to swap in (it loads on a background thread and is picked
        // up by a later process()), then confirm an impulse yields a wet tail.
        bool irReady = false;
        for (int tries = 0; tries < 400 && ! irReady; ++tries)
        {
            AudioBuffer<float> warm(2, 512); warm.clear();
            rev.process(warm, 1.0f);
            if (rev.getCurrentIRSize() > 0) irReady = true;
            else juce::Thread::sleep(5);
        }
        check(irReady, "reverb engine: async IR loaded within timeout");

        // Feed an impulse, then flush enough blocks to capture the whole wet response
        // regardless of the convolution's processing latency. Sum total wet energy.
        // Block size MUST NOT exceed the prepared maximumBlockSize (1024) — the reverb's
        // dry scratch is sized to it; a bigger block overruns it. 128 blocks ~= 2.7 s > IR.
        constexpr int blockLen = 1024;
        double tailEnergy = 0.0;
        bool finiteAll = true;
        for (int blk = 0; blk < 128; ++blk)
        {
            AudioBuffer<float> b(2, blockLen); b.clear();
            if (blk == 0) { b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f); }   // impulse (wet=1, so no dry spike)
            rev.process(b, 1.0f);
            for (int i = 0; i < blockLen; ++i)
            {
                const float v = b.getSample(0, i);
                if (! std::isfinite(v)) finiteAll = false;
                tailEnergy += (double) v * v;
            }
        }
        check(finiteAll, "reverb engine: output finite");
        check(tailEnergy > 1.0e-4, "reverb engine: impulse produces a wet reverb tail");
    }

    std::cout << (failures == 0 ? "ALL PASS" : ("FAILURES: " + String(failures)).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
