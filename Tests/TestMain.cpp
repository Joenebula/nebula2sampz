// Phase 1 AUTO gate: parameter round-trip + automation write/read.
// Uses a minimal dummy AudioProcessor so the parameter layout and APVTS state mechanism
// can be exercised without pulling in the whole plugin/host. Returns non-zero on any
// failure so `ctest` fails the CI job.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/Parameters.h"
#include "../Source/ParameterIDs.h"

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

    std::cout << (failures == 0 ? "ALL PASS" : ("FAILURES: " + String(failures)).toStdString())
              << std::endl;
    return failures == 0 ? 0 : 1;
}
