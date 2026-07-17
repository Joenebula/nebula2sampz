// AUTO-gate tests, run via ctest in CI. Returns non-zero on any failure.
//   Phase 1 — parameter round-trip + automation write/read (dummy AudioProcessor).
//   Phase 2 — master chain (silence, no-clip, finite) + transport parse.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../Source/Parameters.h"
#include "../Source/ParameterIDs.h"
#include "../Source/Presets.h"
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
#include "../Source/dsp/SampleLayer.h"
#include "../Source/dsp/FxGrid.h"
#include "../Source/dsp/MorphPad.h"
#include "../Source/dsp/MorphEngine.h"
#include "../Source/dsp/RackGraph.h"
#include "../Source/dsp/RackModules.h"
#include "../Source/Theme.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <atomic>

//==============================================================================
// A REAL-TIME ALLOCATION DETECTOR.
//
// Replacing global operator new/delete lets us count heap traffic while a specific piece of
// code runs. Anything that allocates on the audio thread can block on the allocator's lock
// and cause a dropout — a defect that is completely invisible to every other test here,
// because the audio is perfectly correct right up until the moment it stutters.
//
// This was on the deferred list since Phase 3, and deferring it cost real bugs: the rack was
// allocating ~8,300 times a SECOND on the audio thread (six juce::dsp::IIR::Coefficients
// factory calls per 32-sample chunk, each one `return *new Coefficients(...)`), and the
// Colour block allocated on every block for every user whether the FX were on or not.
// 385 green assertions had nothing to say about any of it.
namespace rtcheck
{
    std::atomic<int>  allocations { 0 };
    std::atomic<bool> watching { false };

    // Somewhere for the self-check's pointer to escape to, so the compiler can't prove the
    // allocation is unobservable and delete it. C++14 explicitly allows eliding new/delete
    // pairs; clang takes the offer, MSVC didn't, and the difference silently disabled this
    // whole file's RT checking on macOS.
    std::atomic<void*> escape { nullptr };

    struct Scope
    {
        Scope()  { allocations.store(0); watching.store(true); }
        ~Scope() { watching.store(false); }
        int count() const { return allocations.load(); }
    };
}

void* operator new(std::size_t size)
{
    if (rtcheck::watching.load()) rtcheck::allocations.fetch_add(1);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    if (rtcheck::watching.load()) rtcheck::allocations.fetch_add(1);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept                { std::free(p); }
void operator delete[](void* p) noexcept              { std::free(p); }
void operator delete(void* p, std::size_t) noexcept   { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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
            double tail = 0.0;
            for (int blk = 0; blk < 256; ++blk)
            {
                AudioBuffer<float> b(2, 512); b.clear();
                if (blk == 0) { b.setSample(0, 0, 1.0f); b.setSample(1, 0, 1.0f); }
                sp3.process(b, warm);
                if (blk > 0) for (int i = 0; i < 512; ++i) { const float v = b.getSample(0, i); tail += (double) v * v; }
            }
            check(tail > 1.0e-6, "space: reverb send produces a tail");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 4 — sample layer (load, slice, play slices from MIDI)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        SampleLayer layer;
        layer.prepare(48000.0, 512);

        // Nothing loaded: silent, and notes do nothing.
        {
            check(! layer.hasSample(), "sample: starts with no sample");
            AudioBuffer<float> bus(2, 512); bus.clear();
            layer.noteOn(SampleLayer::baseNote, 1.0f);
            layer.render(bus, 0, 512);
            check(bus.getMagnitude(0, 512) == 0.0f, "sample: silent with no sample loaded");
        }

        // Load a synthetic 4-bar-at-140 loop: each slice is a distinct DC level so we can
        // prove the right slice plays. 6.857 s at 48k, sliced into 16.
        const int len = (int) (6.857 * 48000.0);
        {
            AudioBuffer<float> audio(2, len);
            for (int i = 0; i < len; ++i)
            {
                const int slice = juce::jlimit(0, 15, (int) ((double) i / len * 16.0));
                const float v = 0.05f * (float) (slice + 1);      // slice N -> level (N+1)*0.05
                audio.setSample(0, i, v);
                audio.setSample(1, i, v);
            }
            layer.loadBuffer(std::move(audio), 48000.0, "VEC1 Loops BB140 077.wav");
        }

        check(layer.hasSample(), "sample: loads");
        check(layer.getNumSlices() == 16, "sample: slices into 16");
        check(std::abs(layer.getDetectedBpm() - 140.0) < 0.5,
              "sample: tempo detected as 140 from the loop length (not 077 in the name)");

        // Slice 0 plays the first slice's level; slice 8 plays the ninth's.
        const auto playSlice = [&](int sliceIndex)
        {
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + sliceIndex, 1.0f);
            AudioBuffer<float> bus(2, 512); bus.clear();
            layer.render(bus, 0, 512);
            return bus.getSample(0, 100);
        };
        check(std::abs(playSlice(0) - 0.05f) < 0.01f, "sample: note 84 plays slice 0");
        check(std::abs(playSlice(8) - 0.45f) < 0.02f, "sample: note 92 plays slice 8");

        // The attack must survive granular playback: the chop is at full level almost
        // immediately, NOT ramped in over a grain (which would blunt every transient).
        {
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + 8, 1.0f);
            AudioBuffer<float> bus(2, 512); bus.clear();
            layer.render(bus, 0, 512);
            check(std::abs(bus.getSample(0, 4) - 0.45f) < 0.05f,
                  "sample: attack is intact - full level within a few samples, no grain fade-in");
        }

        // THE WHOLE slice must play. Each synthetic slice is a distinct DC level, so if the
        // read ever wraps back to the chop's start the level would drop away from its own.
        // (This is the bug joe heard: chops stopping ~3/4 through and replaying the front.)
        {
            layer.setHostBpm(140.0);            // stretch == 1, so out length == slice length
            layer.setStretchEnabled(true);
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + 8, 1.0f);   // slice 8 -> level 0.45

            int rendered = 0; bool heldLevel = true; int samplesChecked = 0;
            while (layer.activeVoiceCount() > 0 && rendered < 48000 * 2)
            {
                AudioBuffer<float> bus(2, 512); bus.clear();
                layer.render(bus, 0, 512);
                // Ignore the last grain's natural fade-out at the very end of the slice.
                if (rendered < 16000)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = bus.getSample(0, i);
                        ++samplesChecked;
                        if (std::abs(v - 0.45f) > 0.08f) heldLevel = false;
                    }
                rendered += 512;
            }
            check(samplesChecked > 8000, "sample: slice plays for its full length");
            check(heldLevel, "sample: slice reads straight through - never wraps back to its start");
        }

        // Note-off gates the chop: hold it briefly and it stops, rather than running the
        // full slice and piling voices up.
        {
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + 8, 1.0f);
            AudioBuffer<float> bus(2, 512); bus.clear();
            layer.render(bus, 0, 512);
            check(layer.activeVoiceCount() == 1, "sample: chop sounds while held");

            layer.noteOff(SampleLayer::baseNote + 8);
            int guard = 0;
            while (layer.activeVoiceCount() > 0 && guard < 20)
            {
                AudioBuffer<float> b2(2, 512); b2.clear();
                layer.render(b2, 0, 512);
                ++guard;
            }
            check(layer.activeVoiceCount() == 0, "sample: note-off stops the chop (gated, not full length)");
            check(guard <= 2, "sample: note-off releases quickly (~5ms, no long tail)");
        }

        // --- slice modes: grid vs transient, count, sensitivity ---
        {
            // Grid mode honours the count, and re-slicing reuses the same audio.
            SampleLayer::SliceSettings s;
            s.transient = false; s.count = 8;
            layer.setSliceSettings(s);
            check(layer.getNumSlices() == 8, "slice: grid count 8 applied by re-slicing");
            check(layer.hasSample(), "slice: re-slice keeps the sample loaded (no re-decode)");
            check(std::abs(layer.getDetectedBpm() - 140.0) < 0.5, "slice: re-slice preserves detected tempo");

            s.count = 32;
            layer.setSliceSettings(s);
            check(layer.getNumSlices() == 32, "slice: grid count 32 applied");

            s.count = 16;
            layer.setSliceSettings(s);
            check(layer.getNumSlices() == 16, "slice: back to 16");

            // Slices still map correctly after a re-slice (boundaries rebuilt, not stale).
            layer.setHostBpm(140.0);
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + 8, 1.0f);
            AudioBuffer<float> b(2, 512); b.clear();
            layer.render(b, 0, 512);
            check(std::abs(b.getSample(0, 100) - 0.45f) < 0.03f, "slice: slices still map correctly after re-slicing");
        }

        // Transient mode on a signal with clear onsets finds them.
        {
            const double sr = 48000.0;
            const int n = 48000 * 2;
            AudioBuffer<float> hits(2, n);
            hits.clear();
            for (int h = 0; h < 8; ++h)                     // 8 clear bursts in silence
            {
                const int at = h * 11000 + 2000;
                for (int i = 0; i < 700 && at + i < n; ++i)
                {
                    const float v = 0.9f * std::sin(0.25f * (float) i) * std::exp(-0.005f * (float) i);
                    hits.setSample(0, at + i, v); hits.setSample(1, at + i, v);
                }
            }
            SampleLayer tl;
            tl.prepare(sr, 512);
            SampleLayer::SliceSettings ts; ts.transient = true; ts.sensitivity = 0.5f;
            tl.setSliceSettings(ts);
            tl.loadBuffer(std::move(hits), sr, "hits.wav");

            check(tl.getNumSlices() >= 3, "slice: transient mode finds the hits");
            const auto tb = tl.getSliceBoundariesNormalised();
            bool ordered = true;
            for (size_t i = 1; i < tb.size(); ++i) if (tb[i] < tb[i - 1]) ordered = false;
            check(ordered, "slice: transient boundaries in order");
            check(tb.front() <= 0.001f && tb.back() >= 0.999f, "slice: transient spans the whole file");

            const int atHalf = tl.getNumSlices();
            ts.sensitivity = 0.95f;
            tl.setSliceSettings(ts);
            check(tl.getNumSlices() >= atHalf, "slice: raising sensitivity doesn't lose slices");
        }

        // --- state is a contract: the sample must survive a save/load ---
        {
            SampleLayer sl;
            sl.prepare(48000.0, 512);
            check(sl.getSourcePath().isEmpty(), "state: no source path before anything is loaded");

            // Write a real wav, load it, and check the path is recorded.
            auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("nebula2_state_test.wav");
            tmp.deleteFile();
            {
                AudioBuffer<float> src(2, 48000);
                for (int i = 0; i < 48000; ++i)
                {
                    const float v = 0.4f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f);
                    src.setSample(0, i, v); src.setSample(1, i, v);
                }
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::FileOutputStream> os(tmp.createOutputStream());
                if (os != nullptr)
                {
                    std::unique_ptr<juce::AudioFormatWriter> w(
                        wav.createWriterFor(os.release(), 48000.0, 2, 16, {}, 0));
                    if (w != nullptr) w->writeFromAudioSampleBuffer(src, 0, src.getNumSamples());
                }
            }
            check(tmp.existsAsFile(), "state: test wav written");

            SampleLayer a;
            a.prepare(48000.0, 512);
            check(a.loadFile(tmp), "state: loads a real file");
            check(a.getSourcePath() == tmp.getFullPathName(), "state: source path recorded on load");
            const int slicesA = a.getNumSlices();

            // Simulate reopening a saved project: a FRESH layer restoring from that path.
            // Without this, a reopened project comes back with knobs and no break.
            SampleLayer b;
            b.prepare(48000.0, 512);
            check(b.loadFile(juce::File(a.getSourcePath())), "state: restores from the saved path");
            check(b.hasSample(), "state: reopened project has its sample back, not silence");
            check(b.getNumSlices() == slicesA, "state: restored sample slices the same way");

            tmp.deleteFile();
        }

        // --- UI queries the waveform view depends on ---
        {
            std::vector<float> mins, maxs;
            check(layer.getWaveformPeaks(mins, maxs, 200), "waveform: peaks available when loaded");
            check((int) mins.size() == 200 && (int) maxs.size() == 200, "waveform: one min/max per bucket");
            bool sane = true;
            for (size_t i = 0; i < maxs.size(); ++i)
                if (maxs[i] < mins[i] || ! std::isfinite(maxs[i]) || ! std::isfinite(mins[i])) sane = false;
            check(sane, "waveform: peaks finite and max >= min");

            const auto bounds = layer.getSliceBoundariesNormalised();
            check((int) bounds.size() == 17, "waveform: 16 slices -> 17 boundaries");
            check(bounds.front() >= 0.0f && bounds.back() <= 1.0f, "waveform: boundaries normalised 0..1");
            bool ordered = true;
            for (size_t i = 1; i < bounds.size(); ++i) if (bounds[i] < bounds[i - 1]) ordered = false;
            check(ordered, "waveform: boundaries in order");

            // Playhead / highlight reflect what's actually sounding.
            layer.reset();
            check(layer.getPlayingSliceMask() == 0, "waveform: nothing lit when idle");
            check(layer.getPlayheadNormalised() < 0.0f, "waveform: no playhead when idle");

            layer.noteOn(SampleLayer::baseNote + 3, 1.0f);
            AudioBuffer<float> b(2, 512); b.clear();
            layer.render(b, 0, 512);
            check((layer.getPlayingSliceMask() & (1u << 3)) != 0, "waveform: the playing slice is lit");
            const float head = layer.getPlayheadNormalised();
            check(head > 0.15f && head < 0.30f, "waveform: playhead sits inside slice 3");
        }

        // A fast pattern must not exhaust the voice pool and steal from itself.
        {
            layer.reset();
            for (int n = 0; n < 12; ++n)
            {
                layer.noteOn(SampleLayer::baseNote + n, 1.0f);
                layer.noteOff(SampleLayer::baseNote + n);
                AudioBuffer<float> b(2, 512); b.clear();
                layer.render(b, 0, 512);
            }
            check(layer.activeVoiceCount() <= SampleLayer::maxVoices,
                  "sample: gated pattern stays within the voice pool");
        }

        // Out-of-range notes (incl. the drum range) don't trigger the sample layer.
        {
            layer.reset();
            layer.noteOn(36, 1.0f);                      // a kick note
            check(layer.activeVoiceCount() == 0, "sample: drum notes don't trigger slices");
            layer.noteOn(SampleLayer::baseNote + 99, 1.0f);
            check(layer.activeVoiceCount() == 0, "sample: notes past the last slice do nothing");
        }

        // B4 plays the WHOLE break, not a slice — it must run far longer than one chop and
        // sweep the entire file.
        {
            layer.setHostBpm(140.0);          // == detected, so stretch is 1
            layer.setStretchEnabled(true);

            const auto lengthOf = [&](int note)
            {
                layer.reset();
                layer.noteOn(note, 1.0f);
                int rendered = 0;
                while (layer.activeVoiceCount() > 0 && rendered < 48000 * 12)
                {
                    AudioBuffer<float> b(2, 512); b.clear();
                    layer.render(b, 0, 512);
                    rendered += 512;
                }
                return rendered;
            };

            const int oneSlice = lengthOf(SampleLayer::baseNote);
            const int wholeLen = lengthOf(SampleLayer::wholeSampleNote);
            check(wholeLen > oneSlice * 10, "sample: B4 plays the whole break, not one slice");
            check(std::abs((double) wholeLen - (double) len) < 48000 * 0.2,
                  "sample: whole-break length matches the file (~6.86s)");

            // The playhead sweeps the whole file, not just one chop.
            layer.reset();
            layer.noteOn(SampleLayer::wholeSampleNote, 1.0f);
            AudioBuffer<float> b(2, 512); b.clear();
            layer.render(b, 0, 512);
            const float early = layer.getPlayheadNormalised();
            for (int i = 0; i < 300; ++i) { AudioBuffer<float> t(2, 512); t.clear(); layer.render(t, 0, 512); }
            const float later = layer.getPlayheadNormalised();
            check(early >= 0.0f && early < 0.05f, "sample: whole-break playhead starts at the top");
            check(later > early, "sample: whole-break playhead sweeps the file");

            // And it follows the host tempo like everything else.
            layer.setHostBpm(174.0);
            const int fastWhole = lengthOf(SampleLayer::wholeSampleNote);
            layer.setHostBpm(140.0);
            check(fastWhole < wholeLen, "sample: whole break stretches to the host tempo too");
        }

        // A slice stops at its own boundary rather than running on into the next.
        {
            layer.reset();
            layer.noteOn(SampleLayer::baseNote, 1.0f);
            const int sliceLen = len / 16;
            int rendered = 0;
            bool finite = true;
            while (rendered < sliceLen + 4096 && layer.activeVoiceCount() > 0)
            {
                AudioBuffer<float> bus(2, 512); bus.clear();
                layer.render(bus, 0, 512);
                for (int i = 0; i < 512; ++i) if (! std::isfinite(bus.getSample(0, i))) finite = false;
                rendered += 512;
            }
            check(finite, "sample: output finite");
            check(layer.activeVoiceCount() == 0, "sample: slice ends at its boundary and frees its voice");
            check(rendered <= sliceLen + 4096, "sample: slice doesn't run past its length");
        }

        // Velocity scales level.
        {
            layer.reset(); layer.noteOn(SampleLayer::baseNote + 8, 0.25f);
            AudioBuffer<float> soft(2, 512); soft.clear(); layer.render(soft, 0, 512);
            layer.reset(); layer.noteOn(SampleLayer::baseNote + 8, 1.0f);
            AudioBuffer<float> loud(2, 512); loud.clear(); layer.render(loud, 0, 512);
            check(loud.getMagnitude(0, 512) > soft.getMagnitude(0, 512), "sample: velocity scales level");
        }

        // --- granular time-stretch ---
        // How long does a slice sound for, in output samples?
        const auto sliceDuration = [&](double hostBpmValue, bool stretchOn)
        {
            layer.setHostBpm(hostBpmValue);
            layer.setStretchEnabled(stretchOn);
            layer.reset();
            layer.noteOn(SampleLayer::baseNote + 4, 1.0f);
            int rendered = 0;
            while (layer.activeVoiceCount() > 0 && rendered < 48000 * 4)
            {
                AudioBuffer<float> bus(2, 512); bus.clear();
                layer.render(bus, 0, 512);
                rendered += 512;
            }
            return rendered;
        };

        const int natLen  = sliceDuration(140.0, true);   // host == the loop's own 140
        const int fastLen = sliceDuration(174.0, true);   // faster session -> shorter slice
        const int slowLen = sliceDuration(100.0, true);   // slower session -> longer slice
        check(fastLen < natLen, "stretch: a faster session shortens the slice");
        check(slowLen > natLen, "stretch: a slower session lengthens the slice");

        // The ratio should track the tempo ratio (140/174 ~= 0.80), give or take block rounding.
        {
            const double ratio = (double) fastLen / (double) natLen;
            check(std::abs(ratio - (140.0 / 174.0)) < 0.15, "stretch: slice length tracks the tempo ratio");
        }

        // Stretch OFF = native length regardless of host tempo (classic repitch behaviour).
        {
            const int a = sliceDuration(174.0, false);
            const int b = sliceDuration(100.0, false);
            check(std::abs(a - b) <= 512, "stretch: disabled -> host tempo doesn't change length");
        }

        // Unknown host tempo must not stretch (and must not divide by zero).
        {
            const int unknown = sliceDuration(0.0, true);
            check(unknown > 0, "stretch: unknown host tempo still plays");
        }

        // Pitch is preserved: stretch a steady tone and it stays the same frequency.
        // Zero crossings per second is a cheap, robust frequency proxy.
        {
            const int toneLen = 48000 * 2;
            AudioBuffer<float> tone(2, toneLen);
            for (int i = 0; i < toneLen; ++i)
            {
                const float v = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 48000.0f);
                tone.setSample(0, i, v); tone.setSample(1, i, v);
            }
            SampleLayer toneLayer;
            toneLayer.prepare(48000.0, 512);
            { Nebula2::SampleLayer::SliceSettings ss; ss.count = 2; toneLayer.setSliceSettings(ss); }
            toneLayer.loadBuffer(std::move(tone), 48000.0, "tone_120bpm.wav");

            const auto crossingsPerSec = [&](double hostBpmValue)
            {
                toneLayer.setHostBpm(hostBpmValue);
                toneLayer.setStretchEnabled(true);
                toneLayer.reset();
                toneLayer.noteOn(SampleLayer::baseNote, 1.0f);
                int crossings = 0, rendered = 0; float prev = 0.0f;
                while (toneLayer.activeVoiceCount() > 0 && rendered < 48000 * 4)
                {
                    AudioBuffer<float> bus(2, 512); bus.clear();
                    toneLayer.render(bus, 0, 512);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = bus.getSample(0, i);
                        if (prev <= 0.0f && v > 0.0f) ++crossings;
                        prev = v;
                    }
                    rendered += 512;
                }
                return rendered > 0 ? (double) crossings * 48000.0 / (double) rendered : 0.0;
            };

            const double fNative = crossingsPerSec(120.0);
            const double fFast   = crossingsPerSec(174.0);   // much faster session
            check(fNative > 300.0, "stretch: tone plays (crossings detected)");
            // The whole point: time changed, pitch did NOT.
            check(std::abs(fFast - fNative) < fNative * 0.15,
                  "stretch: pitch is preserved when the slice is time-compressed");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 6 — factory presets
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;
        const auto& presets = getFactoryPresets();

        // The rack and scenes live outside the APVTS, so applyPreset takes them too.
        RackGraph pRack;
        juce::SpinLock pRackLock;
        auto pScenes = defaultMorphScenes();

        check(presets.size() >= 10, "presets: a usable factory set exists");

        // Names are unique and non-empty — a duplicate would be indistinguishable in the
        // host's menu.
        {
            juce::StringArray names;
            bool ok = true;
            for (const auto& p : presets)
            {
                const juce::String n(p.name);
                if (n.isEmpty() || names.contains(n)) ok = false;
                names.add(n);
            }
            check(ok, "presets: names are unique and non-empty");
            check(juce::String(presets[0].name) == "Init", "presets: first is Init");
        }

        // Every preset references only REAL parameters, with values inside their range.
        // A typo'd id would silently do nothing — the exact 'looks wired but isn't' trap.
        {
            DummyProcessor dp;
            bool allIdsExist = true, allInRange = true;
            for (const auto& p : presets)
                for (const auto& v : p.values)
                {
                    auto* rp = dp.apvts.getParameter(v.id);
                    if (rp == nullptr) { allIdsExist = false; continue; }
                    const float norm = rp->convertTo0to1(v.value);
                    if (norm < -0.001f || norm > 1.001f) allInRange = false;
                }
            check(allIdsExist, "presets: every id refers to a real parameter (no silent typos)");
            check(allInRange, "presets: every value sits inside its parameter's range");
        }

        // The gate the roadmap names: "preset load applies fully".
        {
            DummyProcessor dp;

            // Dirty the state first, so a partial apply would be visible.
            dp.apvts.getParameter(ParamID::drive)->setValueNotifyingHost(1.0f);
            dp.apvts.getParameter(ParamID::crush)->setValueNotifyingHost(1.0f);
            dp.apvts.getParameter(ParamID::revMix)->setValueNotifyingHost(1.0f);

            // Find "Tube Drive": it sets drive but says nothing about crush/revMix, which
            // must therefore come back to DEFAULT rather than inheriting the dirt.
            int tubeIdx = -1;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (juce::String(presets[(size_t) i].name) == "Tube Drive") tubeIdx = i;
            check(tubeIdx > 0, "presets: Tube Drive present");

            applyPreset(dp.apvts, tubeIdx, pRack, pRackLock, pScenes);

            const float drive = *dp.apvts.getRawParameterValue(ParamID::drive);
            const float crush = *dp.apvts.getRawParameterValue(ParamID::crush);
            const float rev   = *dp.apvts.getRawParameterValue(ParamID::revMix);
            check(std::abs(drive - 60.0f) < 1.5f, "presets: applies its own values (drive 60)");
            check(crush < 1.0f, "presets: params it doesn't mention RESET to default (no leak from before)");
            check(rev < 1.0f, "presets: recall is total, not partial");
        }

        // Switching presets is deterministic: A -> B -> A lands back where it started.
        {
            DummyProcessor dp;
            int dubIdx = -1, tubeIdx = -1;
            for (int i = 0; i < (int) presets.size(); ++i)
            {
                if (juce::String(presets[(size_t) i].name) == "Dub Echo")   dubIdx = i;
                if (juce::String(presets[(size_t) i].name) == "Tube Drive") tubeIdx = i;
            }

            applyPreset(dp.apvts, tubeIdx, pRack, pRackLock, pScenes);
            const float driveA = *dp.apvts.getRawParameterValue(ParamID::drive);
            const float dlyA   = *dp.apvts.getRawParameterValue(ParamID::dlyMix);

            applyPreset(dp.apvts, dubIdx, pRack, pRackLock, pScenes);
            const float dlyB = *dp.apvts.getRawParameterValue(ParamID::dlyMix);
            check(dlyB > dlyA, "presets: Dub Echo actually changes the delay");

            applyPreset(dp.apvts, tubeIdx, pRack, pRackLock, pScenes);
            const float driveBack = *dp.apvts.getRawParameterValue(ParamID::drive);
            const float dlyBack   = *dp.apvts.getRawParameterValue(ParamID::dlyMix);
            check(std::abs(driveBack - driveA) < 0.01f, "presets: A -> B -> A restores A exactly");
            check(std::abs(dlyBack - dlyA) < 0.01f, "presets: no residue from B after returning to A");
        }

        // Init really is the default state.
        {
            DummyProcessor dp;
            dp.apvts.getParameter(ParamID::drive)->setValueNotifyingHost(1.0f);
            applyPreset(dp.apvts, 0, pRack, pRackLock, pScenes);   // Init
            check(*dp.apvts.getRawParameterValue(ParamID::drive) < 1.0f, "presets: Init returns everything to default");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 8 — FX grid sequencer (the prototype's laws, asserted)
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;

        // --- the clock: which step is sounding, from host musical position ---
        check(FxGrid::stepAtPpq(0.0, 16, 0.25) == 0, "grid: ppq 0 -> step 0");
        check(FxGrid::stepAtPpq(0.25, 16, 0.25) == 1, "grid: a 1/16 later -> step 1");
        check(FxGrid::stepAtPpq(0.9, 16, 0.25) == 3, "grid: mid-step stays on that step");
        check(FxGrid::stepAtPpq(4.0, 16, 0.25) == 0, "grid: 16 x 1/16 = 1 bar -> wraps to 0");
        check(FxGrid::stepAtPpq(4.25, 16, 0.25) == 1, "grid: keeps counting after the wrap");
        // Hosts hand you NEGATIVE ppq during count-in/pre-roll. Naive modulo gives a
        // negative index and reads out of bounds.
        check(FxGrid::stepAtPpq(-0.25, 16, 0.25) == 15, "grid: negative ppq (host pre-roll) wraps sanely");
        check(FxGrid::stepAtPpq(1.0, 16, 0.0) == 0, "grid: zero division doesn't divide by zero");

        // --- LAW: a cell is a GATE, not a second volume ---
        // The prototype's bug: strength came from the cell alone, so Shatter at 0% still
        // shattered. The panel must set the ceiling.
        {
            FxGrid g;
            g.setCell((int) GridRow::Drive, 0, 3);   // fully painted

            check(std::abs(g.amountFor(GridRow::Drive, 0.0f, 0) - 0.0f) < 0.001f,
                  "grid LAW: drive at 0% stays SILENT however hard the cell is painted");
            check(std::abs(g.amountFor(GridRow::Drive, 80.0f, 0) - 80.0f) < 0.001f,
                  "grid: a full cell reaches the panel amount, never beyond");
            check(std::abs(g.amountFor(GridRow::Drive, 80.0f, 1) - 0.0f) < 0.001f,
                  "grid: an unpainted step is the effect's neutral (silent)");

            g.setCell((int) GridRow::Drive, 1, 2);   // 2/3
            const float partial = g.amountFor(GridRow::Drive, 90.0f, 1);
            check(partial > 55.0f && partial < 65.0f, "grid: cell level scales the panel amount (2/3 of 90)");
        }

        // --- neutral is per-row: Tone rests OPEN, not at zero ---
        // If every row rested at 0, an unpainted Tone row would slam the filter shut and
        // mute the instrument.
        {
            FxGrid g;
            check(std::abs(g.amountFor(GridRow::Tone, 20.0f, 0) - 100.0f) < 0.001f,
                  "grid: unpainted Tone rests OPEN (100), it does not close and mute");
            check(std::abs(g.amountFor(GridRow::Width, 0.0f, 0) - 100.0f) < 0.001f,
                  "grid: unpainted Width rests unchanged (100)");

            g.setCell((int) GridRow::Tone, 0, 3);
            check(std::abs(g.amountFor(GridRow::Tone, 20.0f, 0) - 20.0f) < 0.001f,
                  "grid: a full Tone cell closes toward YOUR setting (20), not to zero");
            check(gridRowNeutral(GridRow::Drive) == 0.0f, "grid: Drive's neutral is silent");
            check(gridRowNeutral(GridRow::Tone) == 100.0f, "grid: Tone's neutral is open");
        }

        // --- cells: set/get/clear/bounds ---
        {
            FxGrid g;
            g.setCell((int) GridRow::Crush, 5, 2);
            check(g.getCell((int) GridRow::Crush, 5) == 2, "grid: cell set and read back");
            check(g.rowHasAnyCells((int) GridRow::Crush), "grid: row reports it has cells");
            check(! g.rowHasAnyCells((int) GridRow::Delay), "grid: empty row reports empty");

            g.setCell((int) GridRow::Crush, 5, 99);
            check(g.getCell((int) GridRow::Crush, 5) == 3, "grid: cell level clamps to 3");
            g.setCell(-1, 0, 3); g.setCell(0, 999, 3);            // must not crash or corrupt
            check(g.getCell(-1, 0) == 0 && g.getCell(0, 999) == 0, "grid: out-of-range access is safe");

            g.clearRow((int) GridRow::Crush);
            check(! g.rowHasAnyCells((int) GridRow::Crush), "grid: clearRow empties it");

            g.setCell((int) GridRow::Drive, 1, 3);
            g.clearAll();
            check(! g.rowHasAnyCells((int) GridRow::Drive), "grid: clearAll empties everything");
        }

        // --- state: the grid is structured data and must survive save/load intact ---
        {
            FxGrid a;
            a.setNumSteps(32);
            a.setCell((int) GridRow::Drive, 0, 3);
            a.setCell((int) GridRow::Drive, 7, 1);
            a.setCell((int) GridRow::Reverb, 31, 2);
            a.setCell((int) GridRow::Tone, 15, 3);

            FxGrid b;
            b.fromString(a.toString());

            check(b.getNumSteps() == 32, "grid state: step count round-trips");
            check(b.getCell((int) GridRow::Drive, 0) == 3, "grid state: cells round-trip");
            check(b.getCell((int) GridRow::Drive, 7) == 1, "grid state: partial levels round-trip");
            check(b.getCell((int) GridRow::Reverb, 31) == 2, "grid state: the last step round-trips");
            check(b.getCell((int) GridRow::Tone, 15) == 3, "grid state: every row round-trips");
            check(b.toString() == a.toString(), "grid state: save -> load -> save is identical");

            FxGrid c;
            c.fromString("");                       // junk in must not crash
            c.fromString("garbage-not-a-grid");
            check(c.getCell(0, 0) == 0, "grid state: malformed input is ignored safely");
        }

        // --- step count ---
        {
            FxGrid g;
            g.setNumSteps(8);   check(g.getNumSteps() == 8, "grid: 8 steps");
            g.setNumSteps(999); check(g.getNumSteps() == FxGrid::maxSteps, "grid: step count clamps to max");
            g.setNumSteps(0);   check(g.getNumSteps() == 1, "grid: step count can't be zero");
        }
    }

    // ---------------------------------------------------------------------------------
    // Phase 9 — Morph pad: four scenes blended across an X/Y pad
    // ---------------------------------------------------------------------------------
    {
        using namespace Nebula2;
        const auto c = defaultMorphScenes();

        // --- bilinear weights ---
        {
            const auto w = morphWeights(0.0f, 0.0f);
            check(std::abs(w[0] - 1.0f) < 1e-6f, "morph: top-left is corner A alone");
            const auto w2 = morphWeights(1.0f, 1.0f);
            check(std::abs(w2[3] - 1.0f) < 1e-6f, "morph: bottom-right is corner D alone");
            const auto wc = morphWeights(0.5f, 0.5f);
            for (int i = 0; i < 4; ++i)
                check(std::abs(wc[i] - 0.25f) < 1e-6f, "morph: centre weights all four equally");
            // Weights must always sum to 1, or the blend gains/loses level as you move.
            bool sums = true;
            for (float x = 0.0f; x <= 1.0f; x += 0.13f)
                for (float y = 0.0f; y <= 1.0f; y += 0.17f)
                {
                    const auto ww = morphWeights(x, y);
                    if (std::abs((ww[0] + ww[1] + ww[2] + ww[3]) - 1.0f) > 1e-5f) sums = false;
                }
            check(sums, "morph: weights always sum to 1 (no level jump as the dot moves)");
            const auto wOut = morphWeights(-5.0f, 9.0f);
            check(std::abs((wOut[0] + wOut[1] + wOut[2] + wOut[3]) - 1.0f) < 1e-5f,
                  "morph: out-of-range positions are clamped, not broken");
        }

        // --- corners return their scene exactly ---
        {
            const auto a = blendMorph(c, 0.0f, 0.0f);
            check(std::abs(a.cut - c[0].cut) < 1.0f && std::abs(a.drv - c[0].drv) < 0.01f,
                  "morph: the pad's corner IS that scene (A)");
            const auto d = blendMorph(c, 1.0f, 1.0f);
            check(std::abs(d.wid - c[3].wid) < 0.01f && std::abs(d.sht - c[3].sht) < 0.01f,
                  "morph: the pad's corner IS that scene (D)");
            const auto b = blendMorph(c, 1.0f, 0.0f);
            check(std::abs(b.drv - c[1].drv) < 0.01f, "morph: corner B exact");
            const auto cc = blendMorph(c, 0.0f, 1.0f);
            check(std::abs(cc.phs - c[2].phs) < 0.01f, "morph: corner C exact");
        }

        // --- linear params blend linearly ---
        {
            const auto mid = blendMorph(c, 0.5f, 0.0f);   // halfway A..B along the top
            const float wantDrv = (c[0].drv + c[1].drv) * 0.5f;
            check(std::abs(mid.drv - wantDrv) < 0.01f, "morph: drive blends linearly between corners");
        }

        // --- THE ONE THAT MATTERS: cutoff blends in LOG space ---
        // A linear blend of 16k and 700 gives 8.35k — which still sounds "open", so the
        // whole bottom half of the pad's travel would do almost nothing. Geometric gives
        // ~3.3k, which is what "halfway from bright to dark" actually sounds like.
        {
            const auto mid = blendMorph(c, 0.0f, 0.5f);   // halfway A(16k) .. C(700)
            const float linearWould = (c[0].cut + c[2].cut) * 0.5f;         // 8350
            const float geometric   = std::sqrt(c[0].cut * c[2].cut);       // ~3346
            check(std::abs(mid.cut - geometric) < geometric * 0.05f,
                  "morph: cutoff blends GEOMETRICALLY (halfway bright->dark sounds halfway)");
            check(mid.cut < linearWould * 0.6f,
                  "morph: cutoff is NOT a linear blend (which would barely leave 'open')");
        }

        // --- scenes are structured state: they must round-trip ---
        {
            auto edited = defaultMorphScenes();
            edited[1].cut = 1234.5f; edited[1].drv = 77.0f;
            edited[3].wid = 180.0f;  edited[3].sht = 12.5f;

            const auto back = morphScenesFromString(morphScenesToString(edited));
            check(std::abs(back[1].cut - 1234.5f) < 0.1f, "morph state: edited cutoff round-trips");
            check(std::abs(back[1].drv - 77.0f) < 0.01f,  "morph state: edited drive round-trips");
            check(std::abs(back[3].wid - 180.0f) < 0.01f, "morph state: every corner round-trips");
            check(std::abs(back[3].sht - 12.5f) < 0.01f,  "morph state: fractional values survive");

            const auto junk = morphScenesFromString("not a scene at all");
            check(std::abs(junk[0].cut - defaultMorphScenes()[0].cut) < 1.0f,
                  "morph state: malformed input falls back to the seed scenes, never garbage");
            const auto empty = morphScenesFromString("");
            check(std::abs(empty[2].res - defaultMorphScenes()[2].res) < 0.01f,
                  "morph state: empty input is safe");
        }
    }

    // ---- Phase 9b: the Morph ENGINE (the pad's picture is only honest if this is) ----
    {
        using namespace Nebula2;

        const double sr = 44100.0;
        const int block = 512;

        // A sine at `hz`, stereo, identical both sides — so a width change is measurable.
        auto makeTone = [sr, block](float hz)
        {
            AudioBuffer<float> b(2, block);
            for (int i = 0; i < block; ++i)
            {
                const float s = std::sin(MathConstants<float>::twoPi * hz * (float) i / (float) sr);
                b.setSample(0, i, s);
                b.setSample(1, i, s);
            }
            return b;
        };
        auto rms = [](const AudioBuffer<float>& b) { return b.getRMSLevel(0, 0, b.getNumSamples()); };

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };

        // OFF MEANS OFF. Not "off means quiet" — bit-for-bit untouched.
        {
            MorphEngine e; e.prepare(spec); e.reset();
            auto b = makeTone(1000.0f);
            const auto before = b;
            MorphScene dark { 200.0f, 6.0f, 90.0f, 80.0f, 80.0f, 90.0f, 30.0f, 80.0f };
            e.process(b, dark, 120.0, false);
            bool identical = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (b.getSample(ch, i) != before.getSample(ch, i)) identical = false;
            check(identical, "morph engine: off is bit-identical bypass, not just quiet");
        }

        // The filter must actually filter: a dark scene kills a 5k tone, an open one doesn't.
        {
            MorphEngine open; open.prepare(spec); open.reset();
            MorphEngine dark; dark.prepare(spec); dark.reset();
            // Everything neutral except cutoff, so ONLY the filter can explain the difference.
            MorphScene sOpen { 18000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f };
            MorphScene sDark = sOpen; sDark.cut = 200.0f;

            // JUCE's LadderFilter ramps cutoff over 50ms (2205 samples @44.1k). Measuring
            // before that lands is measuring the glide, not the filter — so run past it and
            // judge the LAST block only.
            AudioBuffer<float> bo, bd;
            for (int n = 0; n < 10; ++n)     // 5120 samples > the 2205-sample ramp
            {
                bo = makeTone(5000.0f); bd = makeTone(5000.0f);
                open.process(bo, sOpen, 120.0, true);
                dark.process(bd, sDark, 120.0, true);
            }
            check(rms(bd) < rms(bo) * 0.5f, "morph engine: a dark scene audibly kills a 5k tone");
            check(rms(bo) > 0.1f, "morph engine: an open scene leaves the tone alone");
        }

        // Nothing here may produce a NaN/inf — one bad sample poisons the whole master bus.
        {
            MorphEngine e; e.prepare(spec); e.reset();
            MorphScene hot { 300.0f, 10.0f, 100.0f, 100.0f, 100.0f, 100.0f, 200.0f, 100.0f };
            auto b = makeTone(220.0f);
            bool finite = true;
            for (int n = 0; n < 20; ++n)
            {
                e.process(b, hot, 174.0, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (! std::isfinite(b.getSample(ch, i))) finite = false;
            }
            check(finite, "morph engine: every setting at maximum stays finite");
        }

        // Shatter is a GATE: at full depth it must silence part of the block, not all of it.
        {
            MorphEngine e; e.prepare(spec); e.reset();
            MorphScene s { 18000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 100.0f, 0.0f };
            AudioBuffer<float> b(2, block);
            float quietest = 1.0f, loudest = 0.0f;
            for (int n = 0; n < 16; ++n)   // >1 bar at 120bpm, so the gate must open AND shut
            {
                b = makeTone(440.0f);
                e.process(b, s, 120.0, true);
                quietest = jmin(quietest, rms(b));
                loudest  = jmax(loudest,  rms(b));
            }
            check(quietest < loudest * 0.5f, "morph engine: shatter gates the signal on and off");
            check(loudest > 0.1f, "morph engine: shatter is a gate, not a mute");
        }
    }

    // ---- Phase 10: the modular rack's graph (what's audible, and why) ----
    {
        using namespace Nebula2;

        auto P = [](const char* s) { return parsePort(s); };
        auto patch = [&](RackGraph& g, const char* a, const char* b) { return g.addCable(P(a), P(b)); };

        // --- ports ---
        {
            check(P("flt:cv").valid(), "rack: flt has a cv jack");
            check(! P("eq:cv").valid(), "rack: eq has NO cv jack — patching to it is refused, not ignored");
            check(! P("src:in").valid(), "rack: the beat has no input");
            check(! P("out:out").valid(), "rack: the main out has no output");
            check(! P("nope:in").valid(), "rack: an unknown module is not a port");
            check(! P("flt").valid() && ! P("").valid(), "rack: malformed port text is not a port");
            check(portToString(P("cmb:cv")) == "cmb:cv", "rack: a port round-trips through text");
        }

        // --- an unpatched rack must not silence the track ---
        {
            RackGraph g;
            check(! g.isLive(), "rack: an empty rack is not live (the dry beat still plays)");
            check(g.stateOf(ModuleId::flt) == ModuleState::idle, "rack: an unpatched module is idle");
            check(g.processOrder().empty(), "rack: nothing to process when nothing is patched");
        }

        // --- reachable but trapped: wired, and silent ---
        {
            RackGraph g;
            check(patch(g, "src:out", "flt:in") == PatchResult::ok, "rack: beat -> filter patches");
            check(! g.isLive(), "rack: audio that never reaches the out doesn't make the rack live");
            check(g.stateOf(ModuleId::flt) == ModuleState::noPathOut,
                  "rack: a module with no path out says so, instead of pretending to work");
            check(patch(g, "flt:out", "out:in") == PatchResult::ok, "rack: filter -> out patches");
            check(g.isLive(), "rack: now the rack is live");
            check(g.stateOf(ModuleId::flt) == ModuleState::live, "rack: the filter is live");
        }

        // --- THE PROTOTYPE'S BUG: a branch is not dead ---
        // Its old code walked one straight line from the beat, so a module reached by a
        // second cable was wrongly greyed out. Both branches here are genuinely audible.
        {
            RackGraph g;
            patch(g, "src:out", "flt:in");
            patch(g, "src:out", "cmb:in");    // a SECOND cable off the same out
            patch(g, "flt:out", "out:in");
            patch(g, "cmb:out", "out:in");
            check(g.stateOf(ModuleId::flt) == ModuleState::live, "rack: branch A is live");
            check(g.stateOf(ModuleId::cmb) == ModuleState::live,
                  "rack: branch B is live too — a branch is not dead (the prototype's bug)");
            const auto order = g.processOrder();
            check(order.size() == 2, "rack: both branches get processed");
        }

        // --- a mid-chain module that dead-ends ---
        {
            RackGraph g;
            patch(g, "src:out", "eq:in");
            patch(g, "eq:out",  "out:in");
            patch(g, "src:out", "fld:in");   // patched off the beat, but goes nowhere
            check(g.stateOf(ModuleId::eq)  == ModuleState::live, "rack: the wired-through path is live");
            check(g.stateOf(ModuleId::fld) == ModuleState::noPathOut,
                  "rack: a dead-ending branch is called out, not shown as live");
            const auto order = g.processOrder();
            check(order.size() == 1 && order[0] == ModuleId::eq,
                  "rack: a module that can't be heard costs no CPU");
        }

        // --- cables run one way ---
        {
            RackGraph g;
            check(patch(g, "flt:in", "out:in") == PatchResult::wrongDirection, "rack: in -> in is refused");
            check(patch(g, "src:out", "flt:out") == PatchResult::wrongDirection, "rack: out -> out is refused");
            check(patch(g, "src:out", "flt:in") == PatchResult::ok, "rack: out -> in is the only legal cable");
            check(patch(g, "src:out", "flt:in") == PatchResult::duplicate, "rack: the same cable twice is refused");
        }

        // --- CV is control, not audio ---
        {
            RackGraph g;
            check(patch(g, "lfo:out", "flt:in") == PatchResult::cvIntoAudio,
                  "rack: the LFO can't be patched into an audio input");
            check(patch(g, "src:out", "flt:cv") == PatchResult::audioIntoCV,
                  "rack: audio can't be patched into a CV input");
            check(patch(g, "lfo:out", "flt:cv") == PatchResult::ok, "rack: LFO -> cv patches");

            // A CV cable must not make the filter look like it's carrying audio.
            check(! g.isLive(), "rack: a CV cable alone doesn't make the rack live");
            check(g.stateOf(ModuleId::flt) == ModuleState::noPathOut,
                  "rack: CV alone doesn't put the filter in the audio path");

            patch(g, "src:out", "flt:in");
            patch(g, "flt:out", "out:in");
            check(g.stateOf(ModuleId::lfo) == ModuleState::live,
                  "rack: the LFO is live when what it drives is live");
            check(g.cvSourcesFor(ModuleId::flt).size() == 1, "rack: the filter knows its CV source");
        }

        // --- the LFO driving a dead module is not live ---
        {
            RackGraph g;
            patch(g, "lfo:out", "cmb:cv");   // cmb isn't in any audio path
            check(g.stateOf(ModuleId::lfo) == ModuleState::noPathOut,
                  "rack: an LFO driving an unheard module says so");
        }

        // --- loops are refused, by name ---
        {
            RackGraph g;
            patch(g, "src:out", "flt:in");
            patch(g, "flt:out", "cmb:in");
            check(patch(g, "cmb:out", "flt:in") == PatchResult::wouldLoop,
                  "rack: a cable that closes a loop is refused");
            check(patch(g, "flt:out", "flt:in") == PatchResult::wouldLoop,
                  "rack: a module can't feed itself");
            check(g.processOrder().empty() || g.isLive() == false, "rack: still no path out, so nothing runs");
        }

        // --- process order is a real topological order ---
        {
            RackGraph g;
            patch(g, "src:out", "eq:in");
            patch(g, "eq:out",  "flt:in");
            patch(g, "flt:out", "ech:in");
            patch(g, "ech:out", "out:in");
            const auto order = g.processOrder();
            check(order.size() == 3, "rack: three modules in the chain");
            check(order[0] == ModuleId::eq && order[1] == ModuleId::flt && order[2] == ModuleId::ech,
                  "rack: modules process in signal order, never before their input exists");
        }

        // --- bypass ---
        {
            RackGraph g;
            patch(g, "src:out", "flt:in");
            patch(g, "flt:out", "out:in");
            g.setBypassed(ModuleId::flt, true);
            check(g.stateOf(ModuleId::flt) == ModuleState::off, "rack: a bypassed module reads OFF");
            check(g.isLive(), "rack: bypassing a module doesn't unpatch the rack");
        }

        // --- unplugging ---
        {
            RackGraph g;
            patch(g, "src:out", "flt:in");
            patch(g, "flt:out", "out:in");
            g.removeCablesTouching(ModuleId::flt);
            check(g.getCables().empty(), "rack: pulling a module pulls its cables");
            check(! g.isLive(), "rack: and the rack falls back to the dry beat");
        }

        // --- state: a patch is part of the song ---
        {
            RackGraph g;
            patch(g, "src:out", "flt:in");
            patch(g, "flt:out", "out:in");
            patch(g, "lfo:out", "flt:cv");
            g.setBypassed(ModuleId::ech, true);

            const auto r = RackGraph::fromString(g.toString());
            check(r.getCables().size() == 3, "rack state: every cable round-trips");
            check(r.isLive(), "rack state: the restored patch is live");
            check(r.isBypassed(ModuleId::ech), "rack state: bypass round-trips");
            check(r.stateOf(ModuleId::lfo) == ModuleState::live, "rack state: the CV patch round-trips");

            const auto empty = RackGraph::fromString("");
            check(empty.getCables().empty() && ! empty.isLive(), "rack state: empty input is safe");

            const auto junk = RackGraph::fromString("gibberish;;;flt:out>;>out:in|nope");
            check(junk.getCables().empty(), "rack state: malformed input yields no cables, not garbage");

            // A saved file that predates the no-loops rule must not be able to smuggle one in.
            const auto loop = RackGraph::fromString("src:out>flt:in;flt:out>cmb:in;cmb:out>flt:in|");
            check(loop.getCables().size() == 2, "rack state: a saved loop is dropped on load, not honoured");
        }
    }

    // ---- Phase 10b: the rack's modules actually sound ----
    {
        using namespace Nebula2;

        const double sr = 44100.0;
        const int block = 512;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };

        auto P = [](const char* s) { return parsePort(s); };
        auto makeTone = [sr, block](float hz)
        {
            AudioBuffer<float> b(2, block);
            for (int i = 0; i < block; ++i)
            {
                const float s = 0.5f * std::sin(MathConstants<float>::twoPi * hz * (float) i / (float) sr);
                b.setSample(0, i, s); b.setSample(1, i, s);
            }
            return b;
        };
        auto rms = [](const AudioBuffer<float>& b) { return b.getRMSLevel(0, 0, b.getNumSamples()); };

        // --- vowel table: the numbers that make it talk ---
        {
            float f[3];
            vowelFormantsAt(0.0f, f);
            check(std::abs(f[0] - 730.0f) < 0.5f, "rack vowel: A is the real A formant");
            vowelFormantsAt(4.0f, f);
            check(std::abs(f[0] - 300.0f) < 0.5f, "rack vowel: U is the real U formant");
            vowelFormantsAt(0.5f, f);
            check(std::abs(f[0] - 630.0f) < 0.5f, "rack vowel: it morphs BETWEEN vowels, not snaps");
            vowelFormantsAt(-3.0f, f);
            check(std::abs(f[0] - 730.0f) < 0.5f, "rack vowel: out-of-range clamps, never reads off the table");
            vowelFormantsAt(99.0f, f);
            check(std::abs(f[0] - 300.0f) < 0.5f, "rack vowel: high out-of-range clamps too");
        }

        // --- the wavefolder folds rather than clips ---
        {
            check(std::abs(foldSample(0.0f, 0.5f, 0.0f)) < 1e-6f, "rack folder: silence in, silence out");
            // A clipper is monotonic; a folder is NOT — that's the whole point. Pushed hard,
            // a bigger input must somewhere give a SMALLER output.
            bool foldedBack = false;
            float prev = foldSample(0.0f, 1.0f, 0.0f);
            for (float x = 0.01f; x <= 1.0f; x += 0.01f)
            {
                const float y = foldSample(x, 1.0f, 0.0f);
                if (y < prev - 0.01f) foldedBack = true;
                prev = y;
            }
            check(foldedBack, "rack folder: it FOLDS (non-monotonic), it isn't just a clipper");
            bool bounded = true;
            for (float x = -2.0f; x <= 2.0f; x += 0.05f)
                if (std::abs(foldSample(x, 1.0f, 0.5f)) > 1.0f + 1e-5f) bounded = false;
            check(bounded, "rack folder: output never escapes -1..1");
        }

        // --- an unpatched rack does NOT silence the track ---
        {
            RackEngine e; e.prepare(spec); e.reset();
            RackGraph g; RackDials d;
            auto b = makeTone(440.0f);
            const auto before = b;
            e.process(b, g, d);
            bool identical = true;
            for (int i = 0; i < block; ++i)
                if (b.getSample(0, i) != before.getSample(0, i)) identical = false;
            check(identical, "rack: an unpatched rack leaves the beat bit-identical");
        }

        // --- a patched rack passes audio ---
        {
            RackEngine e; e.prepare(spec); e.reset();
            RackGraph g; RackDials d;
            g.addCable(P("src:out"), P("eq:in"));
            g.addCable(P("eq:out"),  P("out:in"));
            auto b = makeTone(440.0f);
            e.process(b, g, d);
            check(rms(b) > 0.1f, "rack: a patched rack passes the beat through");
        }

        // --- a rack patched to nowhere leaves the beat alone (not silence) ---
        {
            RackEngine e; e.prepare(spec); e.reset();
            RackGraph g; RackDials d;
            g.addCable(P("src:out"), P("flt:in"));   // dead-ends
            auto b = makeTone(440.0f);
            const auto before = b;
            e.process(b, g, d);
            check(std::abs(rms(b) - rms(before)) < 1e-6f,
                  "rack: a dead-ending patch doesn't silence you — the dry beat survives");
        }

        // --- the filter filters ---
        {
            RackGraph g; RackDials d;
            g.addCable(P("src:out"), P("flt:in"));
            g.addCable(P("flt:out"), P("out:in"));

            RackEngine dark; dark.prepare(spec); dark.reset();
            RackDials dd = d; dd.fltCut = 200.0f; dd.fltType = 0;
            AudioBuffer<float> bd;
            for (int i = 0; i < 8; ++i) { bd = makeTone(6000.0f); dark.process(bd, g, dd); }

            RackEngine open; open.prepare(spec); open.reset();
            RackDials od = d; od.fltCut = 18000.0f; od.fltType = 0;
            AudioBuffer<float> bo;
            for (int i = 0; i < 8; ++i) { bo = makeTone(6000.0f); open.process(bo, g, od); }

            check(rms(bd) < rms(bo) * 0.5f, "rack filter: a low cutoff kills a 6k tone");
            check(rms(bo) > 0.05f, "rack filter: an open cutoff passes it");
        }

        // --- bypass passes through dry ---
        {
            RackGraph g; RackDials d;
            d.fltCut = 200.0f;
            g.addCable(P("src:out"), P("flt:in"));
            g.addCable(P("flt:out"), P("out:in"));
            g.setBypassed(ModuleId::flt, true);

            RackEngine e; e.prepare(spec); e.reset();
            AudioBuffer<float> b;
            for (int i = 0; i < 4; ++i) { b = makeTone(6000.0f); e.process(b, g, d); }
            check(rms(b) > 0.2f, "rack: a bypassed filter passes dry — off means off, not silent");
        }

        // --- BRANCHES SUM. This is what the per-module buffers are for. ---
        {
            RackDials d;
            RackGraph one;
            one.addCable(P("src:out"), P("eq:in"));
            one.addCable(P("eq:out"),  P("out:in"));

            RackGraph two;
            two.addCable(P("src:out"), P("eq:in"));
            two.addCable(P("eq:out"),  P("out:in"));
            two.addCable(P("src:out"), P("cho:in"));   // a second, parallel branch
            two.addCable(P("cho:out"), P("out:in"));

            RackEngine e1; e1.prepare(spec); e1.reset();
            RackEngine e2; e2.prepare(spec); e2.reset();
            auto b1 = makeTone(440.0f), b2 = makeTone(440.0f);
            e1.process(b1, one, d);
            e2.process(b2, two, d);
            check(rms(b2) > rms(b1) * 1.1f,
                  "rack: two branches into the out SUM — a branch isn't overwritten");
        }

        // --- runaway feedback is caught by the brick wall ---
        {
            RackGraph g; RackDials d;
            d.echFb = 100.0f; d.echMix = 100.0f; d.cmbFb = 100.0f; d.cmbMix = 100.0f;
            d.outLvl = 100.0f;
            g.addCable(P("src:out"), P("cmb:in"));
            g.addCable(P("cmb:out"), P("ech:in"));
            g.addCable(P("ech:out"), P("out:in"));

            RackEngine e; e.prepare(spec); e.reset();
            bool bounded = true, finite = true;
            for (int n = 0; n < 200; ++n)      // >2 seconds of maximum feedback
            {
                auto b = makeTone(220.0f);
                e.process(b, g, d);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                    {
                        const float s = b.getSample(c, i);
                        if (! std::isfinite(s)) finite = false;
                        if (std::abs(s) > 1.6f) bounded = false;
                    }
            }
            check(finite, "rack: maximum feedback never produces a NaN");
            check(bounded, "rack: maximum feedback can't run away — the brick wall holds");
        }

        // --- CV: the LFO must actually change the sound ---
        {
            RackDials d;
            d.fltCut = 500.0f; d.lfoRate = 6.0f; d.lfoDepth = 100.0f; d.lfoShape = 0;

            RackGraph plain;
            plain.addCable(P("src:out"), P("flt:in"));
            plain.addCable(P("flt:out"), P("out:in"));

            RackGraph modulated = plain;
            modulated.addCable(P("lfo:out"), P("flt:cv"));

            RackEngine e1; e1.prepare(spec); e1.reset();
            RackEngine e2; e2.prepare(spec); e2.reset();

            float spreadPlain = 0.0f, spreadMod = 0.0f;
            float loP = 1.0f, hiP = 0.0f, loM = 1.0f, hiM = 0.0f;
            for (int n = 0; n < 20; ++n)
            {
                auto b1 = makeTone(2000.0f), b2 = makeTone(2000.0f);
                e1.process(b1, plain, d);
                e2.process(b2, modulated, d);
                loP = jmin(loP, rms(b1)); hiP = jmax(hiP, rms(b1));
                loM = jmin(loM, rms(b2)); hiM = jmax(hiM, rms(b2));
            }
            spreadPlain = hiP - loP; spreadMod = hiM - loM;
            check(spreadMod > spreadPlain * 2.0f,
                  "rack CV: patching the LFO to the filter audibly sweeps it");
        }

        // --- a CV cable to a module that isn't in the audio path changes nothing ---
        {
            RackDials d;
            RackGraph g;
            g.addCable(P("src:out"), P("eq:in"));
            g.addCable(P("eq:out"),  P("out:in"));
            RackGraph gCV = g;
            gCV.addCable(P("lfo:out"), P("cmb:cv"));   // cmb is not patched into anything

            RackEngine e1; e1.prepare(spec); e1.reset();
            RackEngine e2; e2.prepare(spec); e2.reset();
            auto b1 = makeTone(440.0f), b2 = makeTone(440.0f);
            e1.process(b1, g, d);
            e2.process(b2, gCV, d);
            bool same = true;
            for (int i = 0; i < block; ++i)
                if (std::abs(b1.getSample(0, i) - b2.getSample(0, i)) > 1e-6f) same = false;
            check(same, "rack CV: CV into an unheard module costs nothing and changes nothing");
        }

        // --- every module, at every extreme, stays finite ---
        {
            const char* mods[] = { "eq", "flt", "phs", "cho", "cmb", "fld", "vow", "ech" };
            bool allFinite = true;
            juce::String firstBad;
            for (const auto* m : mods)
            {
                RackGraph g;
                g.addCable(P("src:out"), parsePort(juce::String(m) + ":in"));
                g.addCable(parsePort(juce::String(m) + ":out"), P("out:in"));

                RackDials d;   // everything hot at once
                d.fltCut = 20.0f; d.fltRes = 18.0f;
                d.phsDepth = 100.0f; d.phsFb = 100.0f; d.phsMix = 100.0f;
                d.choDepth = 100.0f; d.choMix = 100.0f;
                d.cmbTune = 20.0f; d.cmbFb = 100.0f; d.cmbMix = 100.0f;
                d.fldDrive = 100.0f; d.fldSym = 100.0f; d.fldMix = 100.0f;
                d.vowMorph = 4.0f; d.vowSharp = 40.0f; d.vowMix = 100.0f;
                d.echTime = 1.0f; d.echFb = 100.0f; d.echWow = 100.0f; d.echMix = 100.0f;
                for (auto& g2 : d.eqGain) g2 = 18.0f;

                RackEngine e; e.prepare(spec); e.reset();
                for (int n = 0; n < 40; ++n)
                {
                    auto b = makeTone(220.0f);
                    e.process(b, g, d);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < block; ++i)
                            if (! std::isfinite(b.getSample(c, i)))
                            { allFinite = false; if (firstBad.isEmpty()) firstBad = m; }
                }
            }
            check(allFinite, "rack: every module at every extreme stays finite"
                             + (firstBad.isEmpty() ? String() : " (first bad: " + firstBad + ")"));
        }

        // --- a short block must work: hosts do not promise a full one ---
        {
            RackGraph g; RackDials d;
            g.addCable(P("src:out"), P("ech:in"));
            g.addCable(P("ech:out"), P("out:in"));
            RackEngine e; e.prepare(spec); e.reset();
            AudioBuffer<float> b(2, 7);
            b.clear();
            for (int i = 0; i < 7; ++i) { b.setSample(0, i, 0.3f); b.setSample(1, i, 0.3f); }
            e.process(b, g, d);
            bool finite = true;
            for (int i = 0; i < 7; ++i) if (! std::isfinite(b.getSample(0, i))) finite = false;
            check(finite, "rack: a 7-sample block is handled (hosts don't promise a full one)");
        }
    }

    // ---- Phase 10c: the rack is actually WIRED to the host ----
    {
        using namespace Nebula2;
        DummyProcessor proc;

        // Every dial the engine reads must exist as a host parameter. A dial the DAW can't
        // see is a dial that half-works — and this list is exactly the one the processor
        // caches, so a typo in either place fails here rather than silently reading 0.
        const char* dialIDs[] = {
            ParamID::rackOn,
            ParamID::fltCut, ParamID::fltRes, ParamID::fltType,
            ParamID::lfoRate, ParamID::lfoDepth, ParamID::lfoShape,
            ParamID::phsRate, ParamID::phsDepth, ParamID::phsFb, ParamID::phsMix,
            ParamID::choRate, ParamID::choDepth, ParamID::choMix,
            ParamID::cmbTune, ParamID::cmbFb, ParamID::cmbMix,
            ParamID::fldDrive, ParamID::fldSym, ParamID::fldMix,
            ParamID::vowMorph, ParamID::vowSharp, ParamID::vowMix,
            ParamID::echTime, ParamID::echFb, ParamID::echWow, ParamID::echMix,
            ParamID::outLvl,
            ParamID::eqGain0, ParamID::eqGain1, ParamID::eqGain2,
            ParamID::eqGain3, ParamID::eqGain4, ParamID::eqGain5,
        };
        bool allPresent = true;
        String missing;
        for (const auto* id : dialIDs)
            if (proc.apvts.getParameter(id) == nullptr) { allPresent = false; missing = id; }
        check(allPresent, "rack wiring: every rack dial is a real host parameter"
                          + (missing.isEmpty() ? String() : " (missing: " + missing + ")"));

        // The engine reads 33 dial params; the processor caches 33. If those ever disagree
        // the tail of the list silently reads its fallback instead of your knob.
        check((sizeof(dialIDs) / sizeof(dialIDs[0])) == 34,   // 33 dials + rackOn
              "rack wiring: the dial list is the size the processor expects");

        // Defaults must match the engine's, or a fresh rack sounds different from a
        // freshly-reset one.
        RackDials fresh;
        auto pv = [&](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
        check(std::abs(pv(ParamID::fltCut) - fresh.fltCut) < 0.5f, "rack wiring: cutoff default matches the engine");
        check(std::abs(pv(ParamID::cmbFb)  - fresh.cmbFb)  < 0.5f, "rack wiring: comb feedback default matches");
        check(std::abs(pv(ParamID::echTime) - fresh.echTime) < 0.5f, "rack wiring: echo time default matches");
        check(std::abs(pv(ParamID::outLvl) - fresh.outLvl) < 0.5f, "rack wiring: rack out default matches");

        // A rack dial must round-trip through the host's own automation path.
        if (auto* p = proc.apvts.getParameter(ParamID::vowMorph))
        {
            p->setValueNotifyingHost(1.0f);
            check(std::abs(pv(ParamID::vowMorph) - 4.0f) < 0.01f,
                  "rack wiring: automating Vowel to the top reaches U");
            p->setValueNotifyingHost(0.0f);
            check(std::abs(pv(ParamID::vowMorph) - 0.0f) < 0.01f,
                  "rack wiring: and back to A");
        }
    }

    // ---- Phase 6: preset recall is TOTAL (params AND structure) ----
    {
        using namespace Nebula2;
        DummyProcessor proc;
        RackGraph rack;
        juce::SpinLock rackLock;
        auto scenes = defaultMorphScenes();

        const auto& presets = getFactoryPresets();
        check(! presets.empty(), "presets: there are factory presets");

        // Every preset's parameter ids must exist. A typo here silently does nothing —
        // the preset would just quietly not apply that value.
        {
            bool allValid = true;
            String bad;
            for (const auto& p : presets)
                for (const auto& v : p.values)
                    if (proc.apvts.getParameter(v.id) == nullptr)
                    { allValid = false; if (bad.isEmpty()) bad = String(p.name) + "/" + v.id; }
            check(allValid, "presets: every preset id is a real parameter"
                            + (bad.isEmpty() ? String() : " (bad: " + bad + ")"));
        }

        // Every preset's rack patch must actually patch. A typo'd slug would silently
        // yield an empty rack and the preset would sound like nothing.
        {
            bool ok = true;
            String bad;
            for (const auto& p : presets)
            {
                const String patch(p.rackPatch);
                if (patch.isEmpty()) continue;
                const auto g = RackGraph::fromString(patch);
                if (! g.isLive()) { ok = false; if (bad.isEmpty()) bad = p.name; }
            }
            check(ok, "presets: every rack preset's patch actually reaches the Main Out"
                      + (bad.isEmpty() ? String() : " (dead: " + bad + ")"));
        }

        // Every preset's morph scenes must parse to something other than the fallback.
        {
            bool ok = true;
            for (const auto& p : presets)
            {
                const String s(p.morphScenes);
                if (s.isEmpty()) continue;
                const auto parsed = morphScenesFromString(s);
                const auto seed = defaultMorphScenes();
                if (std::abs(parsed[0].cut - seed[0].cut) < 0.001f
                    && std::abs(parsed[3].sht - seed[3].sht) < 0.001f) ok = false;
            }
            check(ok, "presets: a morph preset's scenes parse (not silently the seed set)");
        }

        // THE ONE THAT WAS MISSING. Wire a rack by hand, then load a preset that has no
        // patch: the patch must be GONE. Before this, every dial reset and the previous
        // patch stayed wired — recall to a combination nobody designed.
        {
            rack.addCable(parsePort("src:out"), parsePort("cmb:in"));
            rack.addCable(parsePort("cmb:out"), parsePort("out:in"));
            check(rack.isLive(), "presets: (setup) a hand-wired rack is live");

            int initIdx = -1;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (String(presets[(size_t) i].name) == "Init") initIdx = i;
            check(initIdx >= 0, "presets: there's an Init preset");

            applyPreset(proc.apvts, initIdx, rack, rackLock, scenes);
            check(! rack.isLive(),
                  "presets: loading a preset with no patch CLEARS the rack — recall is total");
        }

        // And the reverse: loading a rack preset must actually wire it.
        {
            int rackIdx = -1;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (String(presets[(size_t) i].name).startsWith("Rack:")) { rackIdx = i; break; }
            check(rackIdx >= 0, "presets: there are rack presets");

            applyPreset(proc.apvts, rackIdx, rack, rackLock, scenes);
            check(rack.isLive(), "presets: loading a rack preset wires the rack");
            check(proc.apvts.getRawParameterValue(ParamID::rackOn)->load() > 0.5f,
                  "presets: a rack preset turns the rack on — it can't be heard otherwise");
        }

        // Morph scenes must be restored to the seed set by a preset that doesn't set them,
        // for exactly the same reason as the patch.
        {
            scenes[0].cut = 12345.0f;
            int initIdx = 0;
            for (int i = 0; i < (int) presets.size(); ++i)
                if (String(presets[(size_t) i].name) == "Init") initIdx = i;
            applyPreset(proc.apvts, initIdx, rack, rackLock, scenes);
            check(std::abs(scenes[0].cut - 12345.0f) > 1.0f,
                  "presets: a preset with no scenes restores the seed scenes, not yours");
        }

        // An out-of-range index must do nothing at all, not half-apply.
        {
            rack.addCable(parsePort("src:out"), parsePort("eq:in"));
            rack.addCable(parsePort("eq:out"), parsePort("out:in"));
            applyPreset(proc.apvts, 9999, rack, rackLock, scenes);
            check(rack.isLive(), "presets: a bad index changes nothing (no half-apply)");
        }
    }

    // ---- Superseded samples are reclaimed (they used to leak for the session) ----
    {
        using namespace Nebula2;
        const double sr = 44100.0;
        const int block = 512;

        auto makeBreak = [](int len)
        {
            AudioBuffer<float> b(2, len);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                    b.setSample(c, i, 0.3f * std::sin((float) i * 0.01f));
            return b;
        };

        SampleLayer layer;
        layer.prepare(sr, block);

        AudioBuffer<float> bus(2, block);
        auto runAudio = [&](int blocks)
        {
            for (int i = 0; i < blocks; ++i) { bus.clear(); layer.render(bus, 0, block); }
        };

        // Load 20 breaks, running audio between them as a real session would. Each
        // SampleData holds a shared_ptr to its audio, so before the fix all 20 stayed
        // alive for the session — roughly 70MB for 10-second stereo breaks.
        for (int i = 0; i < 20; ++i)
        {
            layer.loadBuffer(makeBreak(44100), sr, "break" + String(i));
            runAudio(4);
        }
        check(layer.getRetainedCount() <= 2,
              "sample: superseded breaks are reclaimed — loading 20 doesn't retain 20 (retained: "
              + String(layer.getRetainedCount()) + ")");

        // Re-slicing republishes over the SAME audio; those must be reclaimed too.
        for (int i = 0; i < 20; ++i)
        {
            SampleLayer::SliceSettings s;
            s.count = 4 + i;
            layer.setSliceSettings(s);
            runAudio(4);
        }
        check(layer.getRetainedCount() <= 2,
              "sample: re-slicing repeatedly doesn't pile up either (retained: "
              + String(layer.getRetainedCount()) + ")");

        // The live sample must SURVIVE — a reclaim that frees what's playing is a crash,
        // which is a far worse bug than the leak it replaces.
        check(layer.hasSample(), "sample: the current break is never reclaimed");
        bus.clear();
        layer.noteOn(84, 1.0f);
        layer.render(bus, 0, block);
        check(bus.getMagnitude(0, block) > 0.0f, "sample: and it still plays after all that");

        // With audio STOPPED the count can't advance, so nothing may be freed — we can't
        // prove a render isn't in flight. Conservative is the correct direction here.
        {
            SampleLayer idle;
            idle.prepare(sr, block);
            idle.loadBuffer(makeBreak(4410), sr, "a");
            idle.loadBuffer(makeBreak(4410), sr, "b");   // no render between
            check(idle.getRetainedCount() >= 2,
                  "sample: with audio stopped nothing is freed (can't prove no render is in flight)");
        }
    }

    // ---- The de-allocation refactor must not have changed the SOUND ----
    // Removing the allocations meant swapping IIR::Coefficients::make*() for
    // IIR::ArrayCoefficients::make*(). JUCE documents them as the same maths, but "the docs
    // say so" is not evidence, and this path is the Tone knob every user turns. If these
    // ever diverge, the plugin sounds different and every response test above still passes,
    // because they only assert loose properties like "cuts 5 kHz".
    {
        using namespace Nebula2;
        const double sr = 44100.0;

        // Compare what actually gets STORED, not the raw arrays. Coefficients::assignImpl
        // normalises by a0 and drops it, so a Coefficients holds 5 values where the array
        // has 6 — my first version of this test compared 6 raw against 5 normalised and
        // failed, which looked like a real sound change for a worrying minute. The code was
        // right; the comparison wasn't. Assigning the array is what the DSP does, so that's
        // what to check.
        auto sameCoeffs = [](const std::array<float, 6>& arr,
                             const juce::dsp::IIR::Coefficients<float>::Ptr& ptr)
        {
            if (ptr == nullptr) return false;
            juce::dsp::IIR::Coefficients<float> viaArray;
            viaArray = arr;                       // the same assignImpl the DSP uses
            if (viaArray.coefficients.size() != ptr->coefficients.size()) return false;
            for (int i = 0; i < ptr->coefficients.size(); ++i)
                if (std::abs(viaArray.coefficients[i] - ptr->coefficients[i]) > 1.0e-9f) return false;
            return true;
        };

        bool toneMatches = true;
        for (float t = 0.0f; t <= 1.0f; t += 0.05f)
        {
            const float tn   = juce::jlimit(0.0f, 1.0f, t);
            const float freq = juce::jlimit(20.0f, (float) (sr * 0.49), 200.0f * std::pow(100.0f, tn));
            const float q    = 0.9f + (1.0f - tn) * 6.0f;

            const auto viaArray = ToneFilter::arrayCoefficientsFor(t, sr);
            const auto viaPtr   = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, freq, q);
            if (! sameCoeffs(viaArray, viaPtr)) toneMatches = false;
        }
        check(toneMatches,
              "refactor: Tone's alloc-free coefficients are IDENTICAL to the allocating ones");

        // Same for the rack's paths, which were the ~8,300/sec offenders.
        bool rackMatches = true;
        for (float f = 100.0f; f < 8000.0f; f *= 1.7f)
        {
            if (! sameCoeffs(juce::dsp::IIR::ArrayCoefficients<float>::makeAllPass(sr, f, 0.7f),
                             juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, f, 0.7f)))
                rackMatches = false;
            if (! sameCoeffs(juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass(sr, f, 9.0f),
                             juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, f, 9.0f)))
                rackMatches = false;
            if (! sameCoeffs(juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(sr, f, 1.1f, 2.0f),
                             juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, f, 1.1f, 2.0f)))
                rackMatches = false;
        }
        check(rackMatches,
              "refactor: the rack's phaser/vowel/EQ coefficients are identical too");
    }

    // ---- REAL-TIME SAFETY: the audio path must not touch the heap ----
    {
        using namespace Nebula2;
        const double sr = 44100.0;
        const int block = 512;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };

        auto P = [](const char* s) { return parsePort(s); };
        auto tone = [sr, block]
        {
            AudioBuffer<float> b(2, block);
            for (int i = 0; i < block; ++i)
            {
                const float s = 0.4f * std::sin(MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr);
                b.setSample(0, i, s); b.setSample(1, i, s);
            }
            return b;
        };

        // The heaviest patch: every module live, CV attached, so every coefficient path and
        // the graph walk all run. If anything on the audio path allocates, it's here.
        RackGraph g;
        g.addCable(P("src:out"), P("eq:in"));
        g.addCable(P("eq:out"),  P("flt:in"));
        g.addCable(P("flt:out"), P("phs:in"));
        g.addCable(P("phs:out"), P("cho:in"));
        g.addCable(P("cho:out"), P("cmb:in"));
        g.addCable(P("cmb:out"), P("fld:in"));
        g.addCable(P("fld:out"), P("vow:in"));
        g.addCable(P("vow:out"), P("ech:in"));
        g.addCable(P("ech:out"), P("out:in"));
        g.addCable(P("lfo:out"), P("flt:cv"));
        g.addCable(P("lfo:out"), P("vow:cv"));
        check(g.isLive(), "rt: (setup) the everything-patch is live");

        RackDials d;
        for (auto& e : d.eqGain) e = 6.0f;    // force the EQ coefficient path to run

        RackEngine engine;
        engine.prepare(spec);
        engine.reset();

        // Run once OUTSIDE the watch: first-call lazy setup is allowed to allocate, it's
        // the steady state that must not.
        { auto b = tone(); engine.process(b, g, d); }

        int allocs = 0;
        {
            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i) engine.process(b, g, d);
            allocs = watch.count();
        }
        check(allocs == 0,
              "rt: RackEngine::process allocates NOTHING across 20 blocks (was ~8,300/sec)"
              + (allocs == 0 ? String() : " — got " + String(allocs)));

        // Moving a DIAL must not allocate either — that's the common case while playing.
        {
            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i)
            {
                d.vowMorph = (float) i * 0.2f;      // sweeps the formant coefficients
                d.fltCut = 400.0f + (float) i * 50.0f;
                engine.process(b, g, d);
            }
            const int n = watch.count();
            check(n == 0, "rt: sweeping dials allocates nothing"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The Colour block: the most-executed path in the plugin, and it ran the allocating
        // coefficient factory every block for every user even with FX OFF.
        {
            ColourChain colour;
            colour.prepare(spec);
            colour.reset();
            ColourChain::Params cp;
            cp.on = false;                       // the "off" case allocated too
            { auto b = tone(); colour.process(b, cp); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i) colour.process(b, cp);
            const int n = watch.count();
            check(n == 0, "rt: ColourChain allocates nothing even with FX off"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // And with the FX actually on and the tone knob moving.
        {
            ColourChain colour;
            colour.prepare(spec);
            colour.reset();
            ColourChain::Params cp;
            cp.on = true; cp.drive = 40.0f; cp.squeeze = 30.0f; cp.width = 120.0f;
            { auto b = tone(); colour.process(b, cp); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i) { cp.tone = 20.0f + (float) i * 3.0f; colour.process(b, cp); }
            const int n = watch.count();
            check(n == 0, "rt: ColourChain allocates nothing with FX on and Tone sweeping"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The Morph engine, at its busiest.
        {
            MorphEngine morph;
            morph.prepare(spec);
            morph.reset();
            MorphScene sc { 800.0f, 4.0f, 60.0f, 50.0f, 40.0f, 70.0f, 130.0f, 30.0f };
            { auto b = tone(); morph.process(b, sc, 120.0, true); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i) { sc.cut = 400.0f + (float) i * 100.0f; morph.process(b, sc, 120.0, true); }
            const int n = watch.count();
            check(n == 0, "rt: MorphEngine allocates nothing"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The rest of the real audio path — the modules I had NOT put under the detector
        // when I first wrote it. Finishing the sweep: if any of these allocate per block,
        // it's the same dropout bug the rack had, just in a module I hadn't looked at.

        // Space: parallel reverb (convolution) + tempo-synced ping-pong delay. The delay's
        // time changes with sync/bpm; the reverb IR is fixed until setCharacter (message
        // thread), so processing must not touch the heap.
        {
            SpaceProcessor space;
            space.prepare(spec);
            space.reset();
            SpaceProcessor::Params sp;
            sp.on = true; sp.revMix = 40.0f; sp.dlyMix = 45.0f; sp.dlyFb = 60.0f; sp.bpm = 174.0;
            { auto b = tone(); space.process(b, sp); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i)
            {
                sp.dlyMix = 20.0f + (float) i;      // move the send while it runs
                space.process(b, sp);
            }
            const int n = watch.count();
            check(n == 0, "rt: SpaceProcessor (reverb + delay) allocates nothing"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The delay alone, feedback high, time changing — the path most likely to resize a
        // buffer if any does.
        {
            PingPongDelay delay;
            delay.prepare(spec);
            delay.reset();
            { auto b = tone(); delay.process(b, 0.25f, 0.8f, 0.5f, true); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i)
                delay.process(b, 0.1f + (float) i * 0.02f, 0.85f, 0.5f, true);   // sweep the time
            const int n = watch.count();
            check(n == 0, "rt: PingPongDelay allocates nothing even as the time sweeps"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The master chain: gain -> limiter -> clamp, on every block unconditionally.
        {
            MasterProcessor master;
            master.prepare(spec);
            master.reset();
            { auto b = tone(); master.process(b, 0.9f, true); }

            auto b = tone();
            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i) master.process(b, 0.5f + (float) i * 0.02f, true);
            const int n = watch.count();
            check(n == 0, "rt: MasterProcessor allocates nothing"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The drum kit: MIDI-triggered voices mixed into the bus. noteOn/render claim to be
        // allocation-free (voices are pre-rendered in prepare) — verify the claim.
        {
            DrumKit kit;
            kit.prepare(sr);
            kit.reset();
            AudioBuffer<float> bus(2, block);
            { bus.clear(); kit.noteOn(36, 1.0f); kit.render(bus, 0, block); }

            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i)
            {
                bus.clear();
                kit.noteOn(36 + (i % 8), 0.5f + (float) (i % 2) * 0.4f);   // different voices/velocities
                kit.render(bus, 0, block);
            }
            const int n = watch.count();
            check(n == 0, "rt: DrumKit noteOn/render allocates nothing (pre-rendered voices)"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // The sample layer's render, note trigger included — the granular stretch path.
        {
            SampleLayer layer;
            layer.prepare(sr, block);
            AudioBuffer<float> src(2, 44100);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 44100; ++i) src.setSample(c, i, 0.3f * std::sin((float) i * 0.02f));
            layer.loadBuffer(std::move(src), sr, "rt");

            AudioBuffer<float> bus(2, block);
            { bus.clear(); layer.noteOn(84, 1.0f); layer.render(bus, 0, block); }

            rtcheck::Scope watch;
            for (int i = 0; i < 20; ++i)
            {
                bus.clear();
                if (i % 5 == 0) layer.noteOn(84 + (i % 4), 0.8f);
                layer.render(bus, 0, block);
            }
            const int n = watch.count();
            check(n == 0, "rt: SampleLayer render + noteOn allocates nothing"
                          + (n == 0 ? String() : " — got " + String(n)));
        }

        // Sanity: the detector must actually detect. A test that can only pass is worthless.
        //
        // This failed on macOS while passing on Windows — meaning every "allocates nothing"
        // check above was passing VACUOUSLY there, from a detector counting nothing. The
        // self-check is the only reason that was visible at all.
        //
        // The cause isn't the detector: C++14 permits the compiler to ELIDE a new/delete
        // pair whose result doesn't escape, and clang does what MSVC didn't. `volatile` on
        // the POINTER doesn't help — it constrains the pointer's storage, not the
        // allocation. So make the size opaque and let the pointer escape to an atomic.
        {
            volatile int elems = 4;                 // opaque size: can't be folded away
            rtcheck::Scope watch;
            auto* leak = new int[(size_t) elems];
            rtcheck::escape.store(leak, std::memory_order_relaxed);   // and it must escape
            const int n = watch.count();
            delete[] leak;
            rtcheck::escape.store(nullptr, std::memory_order_relaxed);
            check(n > 0, "rt: (self-check) the allocation detector actually detects — "
                         "without this, every rt check above passes by counting nothing");
        }
    }

    // ---- Bool params report a bool ----
    // pluginval strictness 10 fails at RANDOM on a random subset of the bools:
    //   "Limiter not restored on setStateInformation -- Expected 1, Actual 0.577136"
    // A bool cannot BE 0.577 — but juce::AudioParameterBool::setValue stores whatever float
    // it's handed and getValue() hands it straight back. The APVTS tree holds the snapped
    // 0/1, so save/restore round-trips 1.0 -> 1.0, no change is detected, no listener fires,
    // and the parameter keeps the stale 0.577. Nothing audible: the DSP reads the tree's
    // atomic, which is always 0/1. But it's a real failure of a real release gate.
    //
    // This test pins the fix (SnappedBool in Parameters.h) at the level pluginval checks.
    {
        using namespace Nebula2;
        DummyProcessor proc;

        for (const char* id : { ParamID::limiter, ParamID::fxOn, ParamID::spaceOn,
                                ParamID::padOn, ParamID::gridOn, ParamID::rackOn })
        {
            auto* p = proc.apvts.getParameter(id);
            if (p == nullptr) { check(false, String("bool: ") + id + " exists"); continue; }

            // The exact abuse pluginval performs: a mid-range float into a bool.
            p->setValueNotifyingHost(0.577136f);
            const float v = p->getValue();
            check(v == 0.0f || v == 1.0f,
                  String("bool: ") + id + " reports a bool, not the raw float it was handed");
        }

        // And the round-trip pluginval actually tests: save, disturb, restore, compare —
        // where both the saved and disturbed values land on the SAME side of 0.5, so the
        // tree value never changes and only the raw float can betray you.
        {
            auto* p = proc.apvts.getParameter(ParamID::limiter);
            p->setValueNotifyingHost(0.9f);
            const float saved = p->getValue();

            MemoryBlock mb;
            proc.getStateInformation(mb);

            p->setValueNotifyingHost(0.577136f);   // still "true", different float
            proc.setStateInformation(mb.getData(), (int) mb.getSize());

            check(std::abs(p->getValue() - saved) < 0.1f,
                  "bool: a bool survives save/restore exactly (the pluginval failure)");
        }
    }

    // ---- LAYOUT: controls must be big enough to actually be controls ----
    //
    // Every UI fault on this project has been found by the user squinting at a screenshot:
    // a Sens knob that rendered as literally nothing, "Space On" truncated to "S...", a
    // Morph readout drawn underneath the dot. I cannot see the UI, so these tests check the
    // two mechanical properties that catch that whole family:
    //   1. no control is laid out smaller than its content needs
    //   2. no two controls overlap
    //
    // The specific trap they encode: juce::Rectangle::removeFromLeft does NOT fail when the
    // rectangle is exhausted — it silently returns whatever's left. So a row that runs out
    // of width produces a squashed control, not an error. Widening "Space On" from 86 to
    // 100 px did nothing at all, because only 62 px were ever available.
    {
        using namespace Nebula2;

        struct Probe : juce::Component
        {
            juce::ToggleButton spaceOn { "Space On" };
            juce::ToggleButton fxOn    { "FX On" };
            juce::ToggleButton limiter { "Limiter" };
            juce::ToggleButton rackOn  { "Rack On" };
            juce::ToggleButton gridOn  { "Grid On" };
            juce::ToggleButton morphOn { "Morph On" };
        };
        Probe p;

        // WHAT THIS CAN AND CANNOT DO — read this before trusting it.
        //
        // It does NOT verify rendering. Nothing headless can. It checks that a row isn't
        // over-committed, which is the mechanical half of the bug and the half that keeps
        // biting.
        //
        // Deliberately NOT the font engine. Three tries got here:
        //   1. `given >= getStringWidthInt(...)` — PASSED with the broken 62px. This
        //      binary measures "Space On" at 35px; it truncates at 39px of real space, so
        //      the ruler underestimates and the check was satisfied while the control was
        //      visibly broken. A fresh "appears to verify but doesn't".
        //   2. the same, times 1.5 for headroom — caught 62px on Windows, then FAILED ON
        //      macOS ("needs 114, has 110"), because macOS measures the same string wider.
        //      A font-derived bound fails on whichever platform you didn't develop on.
        //   3. this: a character count. Crude, errs wide, and identical on every platform
        //      and DPI — which is the only property that makes it a usable gate.
        //
        // 7px/char at 11px type is conservative for any sane UI face. If a real string ever
        // needs more, the fix is a wider control, not a cleverer ruler.
        auto minToggleWidth = [](const juce::ToggleButton& b)
        {
            return 15 + 8 + b.getButtonText().length() * 7 + 4;   // box + gap + text + pad
        };

        // The widths PluginEditor::layoutContent actually hands each toggle. Keep this in
        // step with it — that IS the test.
        struct Case { const juce::ToggleButton* b; int given; const char* where; };
        const Case cases[] = {
            { &p.spaceOn, 110, "Space On (rendered as 'S...' at 62)" },
            { &p.fxOn,     90, "FX On" },
            { &p.limiter,  90, "Limiter" },
            { &p.rackOn,   96, "Rack On (was 80 — this test found it)" },
            { &p.gridOn,   96, "Grid On (was 82 — likewise)" },
            { &p.morphOn, 110, "Morph On" },
        };

        bool allFit = true;
        String tooSmall;
        for (const auto& c : cases)
        {
            const int need = minToggleWidth(*c.b);
            if (c.given < need)
            {
                allFit = false;
                if (tooSmall.isEmpty())
                    tooSmall = String(c.where) + " needs " + String(need) + ", has " + String(c.given);
            }
        }
        check(allFit, "layout: every toggle has room for its own label"
                      + (tooSmall.isEmpty() ? String() : " — " + tooSmall));

        // And the arithmetic that actually bit. Panel 660 wide, body reduced 12 a side,
        // panel reduced 10 a side -> 616 usable; three knobs at 90 leave 334 on the right.
        //
        // This is what proved the row was UNFIXABLE by reordering: the combos alone want
        // ~272 of those 334, so the toggle could never have its ~93. Hence two rows. The
        // check is that each row fits on its own.
        {
            const int usable = 660 - 24 - 20;
            const int right  = usable - (3 * 90 + 12);

            const int rowA = 50 + 100 + 12 + 36 + 74;      // revChar + sync labels and boxes
            check(rowA <= right,
                  "layout: the Space combos row fits (" + String(rowA) + " in " + String(right) + ")");

            check(110 >= minToggleWidth(p.spaceOn) && 110 <= right,
                  "layout: Space On gets its own row and fits it");

            // The old single-row total, kept as the evidence: it never fit.
            const int oldSingleRow = rowA + 12 + minToggleWidth(p.spaceOn);
            check(oldSingleRow > right,
                  "layout: the old single row genuinely didn't fit — reordering wouldn't have saved it ("
                  + String(oldSingleRow) + " wanted, " + String(right) + " available)");
        }
    }

    // ---- The dead-control gate ----
    // Every parameter the plugin publishes is listed here. Adding a parameter without
    // adding it here FAILS — which forces the question "what actually reads this?".
    //
    // This gate exists because `bpm` ("Tempo") shipped for weeks: created, published to the
    // DAW, read by NOTHING. Every consumer reads transport.bpm from the host playhead, so
    // automating Tempo did precisely nothing. 363 assertions said nothing about it, because
    // they all tested things that exist rather than noticing something that shouldn't.
    //
    // ONLY add an id here once DSP genuinely reads it. The list isn't the point; the
    // question it forces is.
    {
        using namespace Nebula2;
        DummyProcessor proc;

        const StringArray expected {
            "master", "limiterOn",
            "sliceMode", "sliceCount", "sens",
            "padOn", "padX", "padY",
            "gridOn", "gridSteps",
            "drive", "char", "crush", "squeeze", "tone", "width", "fxOn",
            "revMix", "dlyMix", "dlyFb", "dlySync", "spaceOn", "revChar",
            "rackOn",
            "flt.cut", "flt.res", "flt.type",
            "lfo.rate", "lfo.depth", "lfo.shape",
            "phs.rate", "phs.depth", "phs.fb", "phs.mix",
            "cho.rate", "cho.depth", "cho.mix",
            "cmb.tune", "cmb.fb", "cmb.mix",
            "fld.drive", "fld.sym", "fld.mix",
            "vow.morph", "vow.sharp", "vow.mix",
            "ech.time", "ech.fb", "ech.wow", "ech.mix",
            "out.lvl",
            "eq.0", "eq.1", "eq.2", "eq.3", "eq.4", "eq.5",
        };

        StringArray actual;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*>(p))
                actual.add(rp->getParameterID());

        StringArray unexpected, missing;
        for (const auto& a : actual)
            if (! expected.contains(a)) unexpected.add(a);
        for (const auto& e : expected)
            if (! actual.contains(e)) missing.add(e);

        check(unexpected.isEmpty(),
              "params: nothing is published that this list doesn't know about"
              + (unexpected.isEmpty() ? String() : " (found: " + unexpected.joinIntoString(", ") + ")"));
        check(missing.isEmpty(),
              "params: every expected parameter is published"
              + (missing.isEmpty() ? String() : " (missing: " + missing.joinIntoString(", ") + ")"));

        // The specific corpse, named so it can't quietly return.
        check(proc.apvts.getParameter("bpm") == nullptr,
              "params: there is no Tempo parameter — the host is the clock, not a dead knob");
    }

    std::cout << (failures == 0 ? "ALL PASS" : ("FAILURES: " + String(failures)).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
