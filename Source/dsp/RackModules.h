#pragma once

#include <juce_dsp/juce_dsp.h>
#include "RackGraph.h"

namespace Nebula2
{
    // Every dial in the rack, in one typed struct. The prototype kept these in a loose
    // `dialVals` string map; a struct is the same data with the typos caught at compile
    // time. Defaults are the prototype's own, so a fresh rack sounds like a fresh rack did.
    struct RackDials
    {
        // Ladder filter
        float fltCut = 1200.0f;   // Hz
        float fltRes = 1.0f;      // Q
        int   fltType = 0;        // 0 LP, 1 BP, 2 HP

        // LFO (CV source)
        float lfoRate  = 1.5f;    // Hz
        float lfoDepth = 50.0f;   // %
        int   lfoShape = 0;       // 0 sine, 1 tri, 2 saw, 3 square

        // Phaser
        float phsRate = 0.5f, phsDepth = 75.0f, phsFb = 40.0f, phsMix = 50.0f;

        // Chorus
        float choRate = 0.8f, choDepth = 50.0f, choMix = 50.0f;

        // Comb
        float cmbTune = 180.0f;   // Hz — the delay is 1/tune
        float cmbFb = 80.0f, cmbMix = 50.0f;

        // Wavefolder
        float fldDrive = 35.0f, fldSym = 0.0f, fldMix = 50.0f;

        // Vowel
        float vowMorph = 0.0f;    // 0..4 = A E I O U, continuous
        float vowSharp = 9.0f;    // Q
        float vowMix = 50.0f;

        // Echo
        float echTime = 320.0f;   // ms
        float echFb = 55.0f, echWow = 37.0f, echMix = 50.0f;

        // EQ — one gain per band, dB
        std::array<float, 6> eqGain { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        // Main out
        float outLvl = 100.0f;    // %
    };

    // The vowel formant table — three real formants per vowel, straight from the prototype.
    // Sweep between them and the beat starts pronouncing vowels.
    struct VowelFormants { const char* name; float f[3]; };
    const VowelFormants& vowelAt(int i) noexcept;          // 0..4
    void vowelFormantsAt(float morph, float outF[3]) noexcept;   // continuous, 0..4

    // The wavefolder's transfer curve: fold back on itself up to 4 times, so pushing drive
    // adds harmonics instead of just clipping. `bias` breaks the symmetry (even harmonics).
    float foldSample(float x, float amt, float bias) noexcept;

    // Renders the rack: walks the graph's process order, feeds each module the sum of what
    // reaches its input, and sums whatever reaches the main out.
    //
    // Real-time safe: every buffer is allocated in prepare(), nothing here allocates or
    // locks. Branch topologies work because each module owns an output buffer — the
    // prototype could rely on Web Audio's own graph for that; here it has to be explicit.
    class RackEngine
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // Replaces `buffer` with the rack's output. If the graph isn't live, the buffer is
        // left ALONE — an unpatched rack must never silence the track.
        void process(juce::AudioBuffer<float>& buffer, const RackGraph& graph,
                     const RackDials& dials) noexcept;

        // The LFO's current value, -1..1, for the UI to draw. Read-only, no side effects.
        float lfoValue() const noexcept { return lastLfoValue; }

    private:
        // CV is applied at this granularity. Block-rate CV steps audibly at musical LFO
        // rates; per-sample is wasteful for a filter cutoff. 32 samples is ~0.7ms at 44.1k,
        // far below anything you can hear as a step.
        static constexpr int cvChunk = 32;

        void renderModule(ModuleId m, juce::AudioBuffer<float>& buf, const RackDials& d,
                          float cv, bool hasCV) noexcept;

        double sampleRate = 44100.0;
        int    maxBlock   = 512;

        // One output buffer per module, so a branch can be summed rather than overwritten.
        std::array<juce::AudioBuffer<float>, numRackModules> outBuf;
        juce::AudioBuffer<float> scratch, dryScratch;

        // --- per-module state ---
        std::array<juce::dsp::IIR::Filter<float>, 6> eqL, eqR;

        juce::dsp::StateVariableTPTFilter<float> ladder;

        double lfoPhase = 0.0;
        float  lastLfoValue = 0.0f;

        std::array<juce::dsp::IIR::Filter<float>, 6> apL, apR;   // phaser allpass stages
        double phsPhase = 0.0;
        float  phsFbL = 0.0f, phsFbR = 0.0f;

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> choDelay { 96000 };
        std::array<double, 3> choPhase { 0.0, 0.0, 0.0 };

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> cmbDelay { 96000 };
        juce::dsp::IIR::Filter<float> cmbDampL, cmbDampR;

        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echDelay { 192000 };
        juce::dsp::IIR::Filter<float> echDampL, echDampR;
        double echWowPhase = 0.0;

        std::array<juce::dsp::IIR::Filter<float>, 3> vowL, vowR;

        // Comb and Echo at high feedback WILL run away. The prototype put a brick wall on
        // the rack's output for exactly this reason, and so does this — you must always be
        // able to turn it down rather than reach for the power switch.
        juce::dsp::Limiter<float> limiter;
    };
}
