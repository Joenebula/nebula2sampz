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

    std::cout << (failures == 0 ? "ALL PASS" : ("FAILURES: " + String(failures)).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
