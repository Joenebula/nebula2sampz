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
#include "../Source/dsp/DrumSynth.h"
#include "../Source/dsp/TempoDetect.h"
#include "../Source/dsp/Slicer.h"
#include "../Source/dsp/DrumKit.h"
#include "../Source/dsp/ColourChain.h"
#include "../Source/dsp/SpaceProcessor.h"

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

        // Wait for the async IR to swap in. NOTE: juce::dsp::Convolution starts life with a
        // 1-sample IDENTITY IR (so it passes audio through until a real one lands), so
        // `size > 0` is satisfied instantly by that placeholder and waits for nothing — a
        // real IR here is ~105k samples. Discriminate against the placeholder, or the test
        // races the background loader and passes only by luck.
        bool irReady = false;
        for (int tries = 0; tries < 400 && ! irReady; ++tries)
        {
            AudioBuffer<float> warm(2, 512); warm.clear();
            rev.process(warm, 1.0f);
            if (rev.getCurrentIRSize() > 1) irReady = true;
            else juce::Thread::sleep(5);
        }
        check(irReady, "reverb engine: async IR loaded within timeout (not the 1-sample placeholder)");

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

    // ---------------------------------------------------------------------------------
    // Phase 3 — drum voices (kick, snare)
    // ---------------------------------------------------------------------------------
    {
        namespace D = Nebula2::Drums;
        const double sr = 48000.0;

        const auto kick  = D::vKick(0.8f, 42, sr);
        const auto kick2 = D::vKick(0.8f, 42, sr);
        check((int) kick.size() == 24000, "drum kick: length = dur*sr");
        bool detK = true; for (size_t i = 0; i < kick.size() && detK; i += 13) if (kick[i] != kick2[i]) detK = false;
        check(detK, "drum kick: deterministic for a given seed");
        bool finK = true; for (float v : kick) if (! std::isfinite(v)) { finK = false; break; }
        check(finK, "drum kick: finite (no NaN/inf)");
        check(D::peak(kick) > 0.1f, "drum kick: produces sound");
        check(D::peak(kick) < 2.0f, "drum kick: bounded (no runaway)");

        const auto snare = D::vSnare(0.8f, 7, sr);
        check((int) snare.size() == 14400, "drum snare: length = dur*sr");
        bool finS = true; for (float v : snare) if (! std::isfinite(v)) { finS = false; break; }
        check(finS, "drum snare: finite (no NaN/inf)");
        check(D::peak(snare) > 0.1f, "drum snare: produces sound");
        check(D::peak(snare) < 2.0f, "drum snare: bounded");

        // The seed actually changes the voice (noise/detune differ).
        const auto kA = D::vKick(0.8f, 1, sr);
        const auto kB = D::vKick(0.8f, 2, sr);
        bool differ = false;
        for (size_t i = 0; i < kA.size(); ++i) if (std::abs(kA[i] - kB[i]) > 1.0e-6f) { differ = true; break; }
        check(differ, "drum kick: different seeds -> different voice");

        // Remaining voices: finite, audible, bounded, right length.
        struct VoiceCase { const char* name; std::vector<float> buf; };
        VoiceCase cases[] = {
            { "hat-closed", D::vHat(0.8f, 3, sr, false) },
            { "hat-open",   D::vHat(0.8f, 3, sr, true) },
            { "clap",       D::vClap(0.8f, 5, sr) },
            { "tom",        D::vTom(0.8f, 0, sr) },
            { "rim",        D::vRim(0.8f, 0, sr) },
            { "perc",       D::vPerc(0.8f, 9, sr) },
        };
        for (const auto& vc : cases)
        {
            bool fin = true; for (float x : vc.buf) if (! std::isfinite(x)) { fin = false; break; }
            check(fin, juce::String("drum ") + vc.name + ": finite");
            check(vc.buf.size() > 100, juce::String("drum ") + vc.name + ": non-empty");
            check(D::peak(vc.buf) > 0.02f, juce::String("drum ") + vc.name + ": produces sound");
            check(D::peak(vc.buf) < 2.0f, juce::String("drum ") + vc.name + ": bounded");
        }

        // A noise-based voice is deterministic for its seed.
        check(D::vClap(0.8f, 5, sr) == D::vClap(0.8f, 5, sr), "drum clap: deterministic for a seed");
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — tempo detection ("metadata is a hint; the data is the evidence")
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // The real file that broke the prototype: a 6.857 s loop named "...BB140 077.wav".
        // A naive parser grabs 077 and plays it at double speed. 140 explains the length
        // exactly (4 bars); 77 needs 2.2 bars, so it must be thrown away.
        const double dur = 6.857;

        const auto cands = nameBpmCandidates("VEC1 Loops BB140 077.wav");
        bool has140 = false, has77 = false;
        for (const auto& c : cands) { if (std::abs(c.value - 140.0f) < 0.01f) has140 = true; if (std::abs(c.value - 77.0f) < 0.01f) has77 = true; }
        check(has140 && has77, "tempo: gathers BOTH 140 and 077 from the name (digits glued to letters too)");

        check(fitsDuration(140.0f, dur) > 0.9f, "tempo: 140 explains a 6.857s loop (exactly 4 bars)");
        check(fitsDuration(77.0f, dur) == 0.0f, "tempo: 077 cannot explain it (2.2 bars) -> rejected");

        const auto det = detectTempo(dur, "VEC1 Loops BB140 077.wav");
        check(det.valid, "tempo: detection returns a result");
        check(std::abs(det.bpm - 140.0f) < 0.15f, "tempo: picks 140, not 77 (the evidence wins)");
        check(det.bars == 4, "tempo: reports 4 bars");

        // An explicit "128bpm" is trusted when it also fits the length.
        const double dur128 = (4.0 * 4.0 * 60.0) / 128.0;   // exactly 4 bars at 128
        const auto det128 = detectTempo(dur128, "break_128bpm.wav");
        check(std::abs(det128.bpm - 128.0f) < 0.15f, "tempo: explicit '128bpm' that fits is honoured");

        // A name with no usable number still resolves from the loop length alone.
        const auto detNoName = detectTempo(dur, "untitled.wav");
        check(detNoName.valid, "tempo: falls back to loop-length evidence with no name hint");

        // Junk/short input is rejected rather than guessed.
        check(! detectTempo(0.05, "140.wav").valid, "tempo: too-short buffer returns no result");
    }

    // ---------------------------------------------------------------------------------
    // Phase 3 — slicer maths (zero-snap, grid slices, transients, tempo ratio)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // snapZero: a buffer that flips sign at 50 — an index near it snaps onto the crossing.
        {
            std::vector<float> sq(200);
            for (int i = 0; i < 200; ++i) sq[(size_t) i] = i < 50 ? 1.0f : -1.0f;
            check(snapZero(sq.data(), 200, 52, 10) == 50, "slicer: snapZero finds the zero crossing");
            check(snapZero(sq.data(), 200, 5, 2) == 5, "slicer: snapZero keeps put when no crossing is near");
        }

        // Grid slices: n+1 boundaries, anchored at 0 and the end, strictly increasing.
        {
            const int n = 4800;
            std::vector<float> sine((size_t) n);
            for (int i = 0; i < n; ++i) sine[(size_t) i] = std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * (float) i / 48000.0f);
            const auto s = computeGridSlices(sine.data(), n, 16, 96);
            check((int) s.size() == 17, "slicer: 16 slices -> 17 boundaries");
            check(s.front() == 0 && s.back() == n, "slicer: grid anchored at 0 and end");
            bool inc = true; for (size_t i = 1; i < s.size(); ++i) if (s[i] <= s[i - 1]) inc = false;
            check(inc, "slicer: grid boundaries strictly increasing");
        }

        // Transients: three clear bursts in silence should be found.
        {
            const double sr = 48000.0;
            const int n = 48000;
            std::vector<float> b((size_t) n, 0.0f);
            const int hits[3] = { 4800, 16000, 30000 };
            for (const int h : hits)
                for (int i = 0; i < 600 && h + i < n; ++i)
                    b[(size_t) (h + i)] = 0.9f * std::sin(0.2f * (float) i) * std::exp(-0.004f * (float) i);

            const auto s = detectTransients(b.data(), n, sr, 0.5f);
            check(s.front() == 0 && s.back() == n, "slicer: transients anchored at 0 and end");
            bool inc = true; for (size_t i = 1; i < s.size(); ++i) if (s[i] <= s[i - 1]) inc = false;
            check(inc, "slicer: transient boundaries strictly increasing");
            check(s.size() >= 4, "slicer: finds the bursts in silence");
        }

        // Derived tempo: a 4-bar loop of 6.857 s is 140 BPM — the same evidence the
        // detector uses, from the other direction.
        {
            const int n = (int) (6.857 * 48000.0);
            check(std::abs(derivedBpm(4, n, 48000.0) - 140.0) < 0.2, "slicer: 4 bars over 6.857s -> 140 BPM");
            check(std::abs(tempoRatio(140.0, 140.0) - 1.0) < 1.0e-9, "slicer: tempoRatio unity at native tempo");
            check(std::abs(tempoRatio(70.0, 140.0) - 0.5) < 1.0e-9, "slicer: tempoRatio halves at half tempo");
            check(std::abs(tempoRatio(120.0, 0.0) - 1.0) < 1.0e-9, "slicer: tempoRatio safe with unknown native");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 4 — MIDI-triggered drum kit (the first thing that makes sound)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // GM map matches the prototype's own MIDI exporter.
        check(midiNoteToDrumVoice(36) == (int) DrumVoiceId::Kick,      "kit: note 36 -> kick");
        check(midiNoteToDrumVoice(38) == (int) DrumVoiceId::Snare,     "kit: note 38 -> snare");
        check(midiNoteToDrumVoice(42) == (int) DrumVoiceId::HatClosed, "kit: note 42 -> closed hat");
        check(midiNoteToDrumVoice(46) == (int) DrumVoiceId::HatOpen,   "kit: note 46 -> open hat");
        check(midiNoteToDrumVoice(60) == -1,                            "kit: unmapped note -> none");

        DrumKit kit;
        kit.prepare(48000.0);
        check(kit.isPrepared(), "kit: prepares (pre-renders all voices x velocity layers)");

        // Silent until something is triggered.
        {
            AudioBuffer<float> bus(2, 512); bus.clear();
            kit.render(bus, 0, 512);
            check(bus.getMagnitude(0, 512) == 0.0f, "kit: silent with no notes");
        }

        // A kick note produces sound, and it decays away to silence on its own.
        {
            kit.reset();
            kit.noteOn(36, 0.9f);
            check(kit.activeVoiceCount() == 1, "kit: note-on allocates one voice");

            double energy = 0.0; bool finite = true;
            for (int blk = 0; blk < 64; ++blk)          // 64*512 = 32768 samples > kick length
            {
                AudioBuffer<float> bus(2, 512); bus.clear();
                kit.render(bus, 0, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = bus.getSample(0, i);
                    if (! std::isfinite(v)) finite = false;
                    energy += (double) v * v;
                }
            }
            check(finite, "kit: output finite");
            check(energy > 1.0e-3, "kit: a kick note actually makes sound");
            check(kit.activeVoiceCount() == 0, "kit: voice frees itself once the one-shot ends");
        }

        // An unmapped note triggers nothing (and doesn't crash).
        {
            kit.reset();
            kit.noteOn(60, 0.9f);
            check(kit.activeVoiceCount() == 0, "kit: unmapped note triggers nothing");
        }

        // Polyphony: several voices at once, and it never exceeds the pool.
        {
            kit.reset();
            for (int i = 0; i < 100; ++i) kit.noteOn(36, 0.8f);
            check(kit.activeVoiceCount() <= DrumKit::maxPolyphony, "kit: polyphony capped at the pool size");
            check(kit.activeVoiceCount() > 1, "kit: plays multiple voices at once");
        }

        // Velocity changes the timbre (different layer), not just the level.
        {
            AudioBuffer<float> soft(2, 4096), loud(2, 4096);
            kit.reset(); soft.clear(); kit.noteOn(36, 0.25f); kit.render(soft, 0, 4096);
            kit.reset(); loud.clear(); kit.noteOn(36, 1.0f);  kit.render(loud, 0, 4096);
            check(loud.getMagnitude(0, 4096) > soft.getMagnitude(0, 4096), "kit: harder hits are louder");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 4 — Colour chain (drive/crush/squeeze/tone/width on the bus)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0; spec.maximumBlockSize = 512; spec.numChannels = 2;

        const auto makeTone = []
        {
            AudioBuffer<float> b(2, 512);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    b.setSample(c, i, 0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f));
            return b;
        };

        // Off = off. With fxOn false the block is essentially transparent.
        {
            ColourChain cc; cc.prepare(spec);
            auto b = makeTone();
            const float before = b.getMagnitude(0, 512);
            ColourChain::Params p; p.on = false; p.drive = 100.0f; p.crush = 100.0f; p.squeeze = 100.0f;
            cc.process(b, p);
            const float after = b.getMagnitude(0, 512);
            check(std::abs(after - before) < before * 0.15f, "colour: fxOn=false is neutral even with everything maxed");
        }

        // Drive changes the signal and stays bounded/finite.
        {
            ColourChain cc; cc.prepare(spec);
            auto dry = makeTone();
            auto wet = makeTone();
            ColourChain::Params p; p.on = true; p.drive = 80.0f; p.driveChar = 0;
            for (int i = 0; i < 8; ++i) { wet = makeTone(); cc.process(wet, p); }   // let the gain smoothing settle
            bool differs = false;
            for (int i = 0; i < 512; ++i) if (std::abs(wet.getSample(0, i) - dry.getSample(0, i)) > 1.0e-3f) { differs = true; break; }
            check(differs, "colour: drive actually changes the signal");
            check(std::isfinite(wet.getMagnitude(0, 512)), "colour: output finite under drive");
            check(wet.getMagnitude(0, 512) < 4.0f, "colour: output bounded under drive");
        }

        // Every drive character is finite and bounded (fold is the wild one).
        {
            for (int ch = 0; ch <= 2; ++ch)
            {
                ColourChain cc; cc.prepare(spec);
                auto b = makeTone();
                ColourChain::Params p; p.on = true; p.drive = 100.0f; p.driveChar = ch;
                for (int i = 0; i < 4; ++i) { b = makeTone(); cc.process(b, p); }
                check(std::isfinite(b.getMagnitude(0, 512)), juce::String("colour: drive char ") + juce::String(ch) + " finite");
                check(b.getMagnitude(0, 512) < 4.0f, juce::String("colour: drive char ") + juce::String(ch) + " bounded");
            }
        }

        // Crush at full changes the signal; tone closed darkens it.
        {
            ColourChain cc; cc.prepare(spec);
            auto b = makeTone();
            ColourChain::Params p; p.on = true; p.crush = 100.0f;
            cc.process(b, p);
            check(std::isfinite(b.getMagnitude(0, 512)), "colour: crush finite");

            // Tone closed is a RESONANT lowpass: 200 Hz with Q 6.9 (the prototype's
            // Q = 0.9 + (1-tone)*6). So it does two things, and both are intended:
            const auto makeAt = [](float freq)
            {
                AudioBuffer<float> b(2, 512);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        b.setSample(c, i, 0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * freq * (float) i / 48000.0f));
                return b;
            };
            const auto settled = [&](float toneVal, float freq)
            {
                ColourChain cc4; cc4.prepare(spec);
                ColourChain::Params pp; pp.on = true; pp.tone = toneVal;
                AudioBuffer<float> b(2, 512);
                for (int i = 0; i < 6; ++i) { b = makeAt(freq); cc4.process(b, pp); }
                return b.getMagnitude(0, 512);
            };

            // 1. well above the cutoff, closed cuts hard
            check(settled(0.0f, 5000.0f) < settled(100.0f, 5000.0f) * 0.5f,
                  "colour: tone closed cuts the highs (5 kHz) vs open");
            // 2. AT the cutoff it resonates — closed boosts 200 Hz. That's the character,
            //    not a bug (it's why a tone sweep sings).
            check(settled(0.0f, 200.0f) > settled(100.0f, 200.0f) * 2.0f,
                  "colour: tone closed resonates at its cutoff (200 Hz peak)");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 4 — Space send (tempo-synced delay + reverb, parallel)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // Musical time, never milliseconds: an 1/8 at 120 BPM is 0.25 s, and it follows
        // the tempo (at 140 the same division is shorter).
        check(std::abs(delayTimeSeconds(DelaySync::Eighth, 120.0) - 0.25) < 1.0e-9, "space: 1/8 at 120 BPM = 0.25 s");
        check(std::abs(delayTimeSeconds(DelaySync::Quarter, 120.0) - 0.5) < 1.0e-9, "space: 1/4 at 120 BPM = 0.5 s");
        check(delayTimeSeconds(DelaySync::Eighth, 140.0) < delayTimeSeconds(DelaySync::Eighth, 120.0),
              "space: the echo follows the host tempo (faster BPM -> shorter delay)");
        check(std::abs(delayTimeSeconds(DelaySync::Eighth, 0.0) - 0.25) < 1.0e-9, "space: safe fallback when BPM is unknown");

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = 48000.0; spec.maximumBlockSize = 512; spec.numChannels = 2;

        SpaceProcessor sp;
        sp.prepare(spec);

        const auto makeImpulse = []
        {
            AudioBuffer<float> b(2, 512); b.clear();
            b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f);
            return b;
        };

        // Zero means zero: both sends down = untouched dry.
        {
            auto b = makeImpulse();
            AudioBuffer<float> ref(b);
            SpaceProcessor::Params p; p.revMix = 0.0f; p.dlyMix = 0.0f;
            sp.process(b, p);
            float maxDiff = 0.0f;
            for (int i = 0; i < 512; ++i) maxDiff = std::max(maxDiff, std::abs(b.getSample(0, i) - ref.getSample(0, i)));
            check(maxDiff == 0.0f, "space: both sends at zero leaves the dry signal untouched");
        }

        // spaceOn=false is neutral even with the sends open.
        {
            auto b = makeImpulse();
            AudioBuffer<float> ref(b);
            SpaceProcessor::Params p; p.revMix = 100.0f; p.dlyMix = 100.0f; p.on = false;
            sp.process(b, p);
            float maxDiff = 0.0f;
            for (int i = 0; i < 512; ++i) maxDiff = std::max(maxDiff, std::abs(b.getSample(0, i) - ref.getSample(0, i)));
            check(maxDiff == 0.0f, "space: off means off, even with sends maxed");
        }

        // The delay send adds echoes while preserving the dry hit.
        {
            SpaceProcessor sp2; sp2.prepare(spec);
            SpaceProcessor::Params p; p.revMix = 0.0f; p.dlyMix = 100.0f; p.dlyFb = 50.0f;
            p.dlySync = DelaySync::Sixteenth; p.bpm = 120.0;   // 0.125 s = 6000 samples

            double echoEnergy = 0.0; bool finite = true; float dryHit = 0.0f;
            for (int blk = 0; blk < 24; ++blk)
            {
                AudioBuffer<float> b(2, 512); b.clear();
                if (blk == 0) { b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f); }
                sp2.process(b, p);
                if (blk == 0) dryHit = b.getSample(0, 0);
                else for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample(0, i);
                    if (! std::isfinite(v)) finite = false;
                    echoEnergy += (double) v * v;
                }
            }
            check(std::abs(dryHit - 1.0f) < 1.0e-4f, "space: delay send preserves the dry hit");
            check(finite, "space: delay send output finite");
            check(echoEnergy > 1.0e-4, "space: delay send produces echoes");
        }

        // The reverb send adds a tail once its IR has loaded.
        {
            SpaceProcessor sp3; sp3.prepare(spec);
            SpaceProcessor::Params warm; warm.revMix = 100.0f; warm.dlyMix = 0.0f;
            // > 1, not > 0: JUCE seeds the convolution with a 1-sample identity IR.
            bool irReady = false;
            for (int tries = 0; tries < 400 && ! irReady; ++tries)
            {
                AudioBuffer<float> b(2, 512); b.clear();
                sp3.process(b, warm);
                if (sp3.reverbIRSize() > 1) irReady = true; else juce::Thread::sleep(5);
            }
            check(irReady, "space: reverb IR loads (real IR, not the placeholder)");

            // Flush well past the IR length + convolution latency before judging.
            double tail = 0.0, peakAbs = 0.0;
            int firstNonZeroBlk = -1;
            for (int blk = 0; blk < 256; ++blk)
            {
                AudioBuffer<float> b(2, 512); b.clear();
                if (blk == 0) { b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f); }
                sp3.process(b, warm);
                if (blk > 0)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = b.getSample(0, i);
                        tail += (double) v * v;
                        peakAbs = std::max(peakAbs, (double) std::abs(v));
                        if (firstNonZeroBlk < 0 && std::abs(v) > 1.0e-9f) firstNonZeroBlk = blk;
                    }
            }
            // Diagnostics: two wrong guesses is enough — print what's actually happening.
            std::cout << "  [diag] space reverb: IRsize=" << sp3.reverbIRSize()
                      << " tailEnergy=" << tail << " peak=" << peakAbs
                      << " firstNonZeroBlk=" << firstNonZeroBlk << std::endl;

            // Same probe straight through a bare Reverb, for comparison.
            {
                Nebula2::Reverb bare;
                bare.prepare(spec);
                bool ready = false;
                for (int t = 0; t < 400 && ! ready; ++t)
                {
                    AudioBuffer<float> w(2, 512); w.clear();
                    bare.process(w, 1.0f);
                    if (bare.getCurrentIRSize() > 1) ready = true; else juce::Thread::sleep(5);
                }
                double bareTail = 0.0;
                for (int blk = 0; blk < 256; ++blk)
                {
                    AudioBuffer<float> b(2, 512); b.clear();
                    if (blk == 0) { b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f); }
                    bare.process(b, 1.0f);
                    if (blk > 0) for (int i = 0; i < 512; ++i) { const float v = b.getSample(0, i); bareTail += (double) v * v; }
                }
                std::cout << "  [diag] bare reverb: IRsize=" << bare.getCurrentIRSize()
                          << " tailEnergy=" << bareTail << std::endl;
            }

            check(tail > 1.0e-6, "space: reverb send produces a tail");
        }
    }

    std::cout << (failures == 0 ? "ALL PASS" : ("FAILURES: " + String(failures)).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
