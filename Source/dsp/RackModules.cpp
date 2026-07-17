#include "RackModules.h"

#include <cmath>

namespace Nebula2
{
    namespace
    {
        const VowelFormants vowels[5] =
        {
            { "A", { 730.0f, 1090.0f, 2440.0f } },
            { "E", { 530.0f, 1840.0f, 2480.0f } },
            { "I", { 270.0f, 2290.0f, 3010.0f } },
            { "O", { 570.0f,  840.0f, 2410.0f } },
            { "U", { 300.0f,  870.0f, 2240.0f } },
        };

        // The EQ's fixed band layout, from the prototype. Only the gains are dialled.
        struct EqBand { int type; float f, q; };   // type: 0 peak, 1 lowshelf, 2 highshelf
        const EqBand eqBands[6] =
        {
            { 1,   35.0f, 0.71f },     // the prototype's band 0 is a highpass, off by default;
                                       // as a shelf at unity it is the same "does nothing".
            { 1,  110.0f, 0.71f },
            { 0,  420.0f, 1.10f },
            { 0, 1600.0f, 1.10f },
            { 0, 5200.0f, 1.10f },
            { 2, 9000.0f, 0.71f },
        };

        float lfoShapeValue(int shape, double phase) noexcept
        {
            const float t = (float) phase;   // 0..1
            switch (shape)
            {
                case 1:  return 1.0f - 4.0f * std::abs (std::round (t) - t);       // triangle
                case 2:  return 2.0f * t - 1.0f;                                   // saw
                case 3:  return t < 0.5f ? 1.0f : -1.0f;                           // square
                default: return std::sin (t * juce::MathConstants<float>::twoPi);  // sine
            }
        }

        inline float wetOf (float mixPercent) noexcept { return mixPercent / 100.0f; }
        // The prototype's dry law: dry = 1 - mix/200. At mix=100 the dry is still 0.5, so a
        // wet-heavy setting thickens rather than replaces. Keep it — it's why the modules
        // stack without collapsing.
        inline float dryOf (float mixPercent) noexcept { return 1.0f - mixPercent / 200.0f; }
    }

    const VowelFormants& vowelAt(int i) noexcept
    {
        return vowels[juce::jlimit (0, 4, i)];
    }

    void vowelFormantsAt(float morph, float outF[3]) noexcept
    {
        const float p  = juce::jlimit (0.0f, 4.0f, morph);
        const int   i0 = (int) std::floor (p);
        const int   i1 = juce::jmin (4, i0 + 1);
        const float fr = p - (float) i0;
        for (int k = 0; k < 3; ++k)
            outF[k] = vowels[i0].f[k] + (vowels[i1].f[k] - vowels[i0].f[k]) * fr;
    }

    float foldSample(float x, float amt, float bias, float preGain) noexcept
    {
        // The CV pre-gain scales the signal, then the shaper clamps its input to [-1,1]
        // before the curve — so a hot pre-gain pins the signal into the curve's fully
        // folded extremes. That clamp is the WaveShaper's, not decoration; without it the
        // CV path would fold linearly forever instead of matching Web Audio.
        const float idx = juce::jlimit (-1.0f, 1.0f, x * preGain);
        float y = (idx + bias) * (1.0f + amt * 6.0f);
        for (int k = 0; k < 4; ++k)
            if (std::abs (y) > 1.0f)
                y = (y > 0.0f ? 1.0f : -1.0f) * (2.0f - std::abs (y));
        return juce::jlimit (-1.0f, 1.0f, y);
    }

    void RackEngine::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        maxBlock   = (int) spec.maximumBlockSize;

        for (auto& b : outBuf) b.setSize (2, maxBlock, false, true, true);
        scratch.setSize (2, maxBlock, false, true, true);
        dryScratch.setSize (2, maxBlock, false, true, true);

        juce::dsp::ProcessSpec mono { spec.sampleRate, spec.maximumBlockSize, 1 };

        for (int i = 0; i < 6; ++i) { eqL[i].prepare (mono); eqR[i].prepare (mono); }
        for (int i = 0; i < 6; ++i) { apL[i].prepare (mono); apR[i].prepare (mono); }
        for (int i = 0; i < 3; ++i) { vowL[i].prepare (mono); vowR[i].prepare (mono); }

        // Prime every filter's coefficient storage HERE, on the message thread. A default
        // Coefficients holds an empty Array, so the first in-place assign would call
        // ensureStorageAllocated and allocate — once, on the audio thread, which is exactly
        // the thing we're removing. Assigning now means the capacity already exists and
        // every later assign is a pure overwrite.
        {
            const auto seed = juce::dsp::IIR::ArrayCoefficients<float>::makeAllPass (sampleRate, 1000.0f, 0.7f);
            for (int i = 0; i < 6; ++i) { *eqL[i].coefficients = seed;  *eqR[i].coefficients = seed; }
            for (int i = 0; i < 6; ++i) { *apL[i].coefficients = seed;  *apR[i].coefficients = seed; }
            for (int i = 0; i < 3; ++i) { *vowL[i].coefficients = seed; *vowR[i].coefficients = seed; }
        }

        ladder.prepare (spec);
        ladder.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

        choDelay.prepare (spec);  choDelay.setMaximumDelayInSamples ((int) (0.06 * sampleRate) + 4);
        cmbDelay.prepare (spec);  cmbDelay.setMaximumDelayInSamples ((int) (0.05 * sampleRate) + 4);
        echDelay.prepare (spec);  echDelay.setMaximumDelayInSamples ((int) (2.0  * sampleRate) + 4);

        cmbDampL.prepare (mono); cmbDampR.prepare (mono);
        echDampL.prepare (mono); echDampR.prepare (mono);
        cmbDampL.coefficients = cmbDampR.coefficients =
            juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 6000.0);
        echDampL.coefficients = echDampR.coefficients =
            juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 3200.0);

        limiter.prepare (spec);
        limiter.setThreshold (-3.0f);
        limiter.setRelease (100.0f);

        reset();
    }

    void RackEngine::reset()
    {
        for (auto& b : outBuf) b.clear();
        scratch.clear();
        dryScratch.clear();

        for (int i = 0; i < 6; ++i) { eqL[i].reset(); eqR[i].reset(); apL[i].reset(); apR[i].reset(); }
        for (int i = 0; i < 3; ++i) { vowL[i].reset(); vowR[i].reset(); }
        ladder.reset();
        choDelay.reset(); cmbDelay.reset(); echDelay.reset();
        cmbDampL.reset(); cmbDampR.reset(); echDampL.reset(); echDampR.reset();
        limiter.reset();

        lfoPhase = phsPhase = echWowPhase = 0.0;
        choPhase = { 0.0, 0.0, 0.0 };
        phsFbL = phsFbR = 0.0f;
        lastLfoValue = 0.0f;
    }

    void RackEngine::renderModule(ModuleId m, juce::AudioBuffer<float>& buf, const RackDials& d,
                                  float cv, bool hasCV) noexcept
    {
        const int n  = buf.getNumSamples();
        const int ch = buf.getNumChannels();
        if (n <= 0 || ch <= 0) return;

        auto* L = buf.getWritePointer (0);
        auto* R = ch > 1 ? buf.getWritePointer (1) : L;

        switch (m)
        {
            case ModuleId::eq:
            {
                for (int i = 0; i < 6; ++i)
                {
                    const auto& b = eqBands[i];
                    const float g = juce::jlimit (-18.0f, 18.0f, d.eqGain[(size_t) i]);
                    // A band at 0dB is a no-op. Zero means zero: don't run a filter that
                    // can only add phase shift and CPU for nothing.
                    if (std::abs (g) < 0.01f) continue;

                    const float gain = juce::Decibels::decibelsToGain (g);
                    const auto coeffs = b.type == 1
                        ? juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf  (sampleRate, b.f, b.q, gain)
                        : b.type == 2
                        ? juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, b.f, b.q, gain)
                        : juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, b.f, b.q, gain);

                    *eqL[(size_t) i].coefficients = coeffs;
                    *eqR[(size_t) i].coefficients = coeffs;
                    for (int s = 0; s < n; ++s)
                    {
                        L[s] = eqL[(size_t) i].processSample (L[s]);
                        if (ch > 1) R[s] = eqR[(size_t) i].processSample (R[s]);
                    }
                }
                break;
            }

            case ModuleId::flt:
            {
                ladder.setType (d.fltType == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                              : d.fltType == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                               : juce::dsp::StateVariableTPTFilterType::lowpass);
                ladder.setResonance (juce::jlimit (0.05f, 4.0f, d.fltRes * 0.4f));

                // CV scale 4000Hz, as the prototype: the LFO sweeps the cutoff by up to 4k.
                const float base = juce::jlimit (20.0f, (float) (sampleRate * 0.45), d.fltCut);
                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    const float c = hasCV ? juce::jlimit (20.0f, (float) (sampleRate * 0.45),
                                                          base + cv * 4000.0f)
                                          : base;
                    ladder.setCutoffFrequency (c);
                    for (int i = s; i < s + len; ++i)
                    {
                        L[i] = ladder.processSample (0, L[i]);
                        if (ch > 1) R[i] = ladder.processSample (1, R[i]);
                    }
                }
                break;
            }

            case ModuleId::phs:
            {
                const float wet = wetOf (d.phsMix), dry = dryOf (d.phsMix);
                const float fb  = juce::jlimit (0.0f, 0.9f, d.phsFb / 100.0f * 0.9f);
                const double inc = d.phsRate / sampleRate;

                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    const float mod = std::sin ((float) phsPhase * juce::MathConstants<float>::twoPi)
                                      * d.phsDepth * 12.0f;
                    for (int i = 0; i < 6; ++i)
                    {
                        const float f = juce::jlimit (40.0f, (float) (sampleRate * 0.45),
                                                      400.0f * std::pow (1.7f, (float) i) + mod);
                        // ArrayCoefficients, NOT IIR::Coefficients. The latter's factories are
                        // literally `return *new Coefficients(...)` — a heap allocation, and
                        // this one is 6 per 32-sample chunk: ~8,300 allocations/second on the
                        // AUDIO THREAD. ArrayCoefficients returns a std::array by value and
                        // assigns in place. Identical maths, no heap.
                        const auto co = juce::dsp::IIR::ArrayCoefficients<float>::makeAllPass (sampleRate, f, 0.7f);
                        *apL[(size_t) i].coefficients = co;
                        *apR[(size_t) i].coefficients = co;
                    }
                    for (int i = s; i < s + len; ++i)
                    {
                        const float dL = L[i], dR = R[i];
                        float wL = dL + phsFbL * fb, wR = dR + phsFbR * fb;
                        for (int k = 0; k < 6; ++k)
                        {
                            wL = apL[(size_t) k].processSample (wL);
                            if (ch > 1) wR = apR[(size_t) k].processSample (wR);
                        }
                        phsFbL = wL; phsFbR = wR;
                        L[i] = dL * dry + wL * wet;
                        if (ch > 1) R[i] = dR * dry + wR * wet;
                    }
                    phsPhase += inc * len;
                    if (phsPhase >= 1.0) phsPhase -= std::floor (phsPhase);
                }
                break;
            }

            case ModuleId::cho:
            {
                const float wet = wetOf (d.choMix), dry = dryOf (d.choMix);
                const float depth = d.choDepth / 100.0f * 0.008f;

                for (int i = 0; i < n; ++i)
                {
                    const float dL = L[i], dR = R[i];
                    choDelay.pushSample (0, dL);
                    if (ch > 1) choDelay.pushSample (1, dR);

                    float sumL = 0.0f, sumR = 0.0f;
                    for (int v = 0; v < 3; ++v)
                    {
                        const float base = 0.012f + (float) v * 0.007f;
                        const float mod  = std::sin ((float) choPhase[(size_t) v]
                                                     * juce::MathConstants<float>::twoPi) * depth;
                        const float t = juce::jlimit (0.0005f, 0.059f, base + mod);
                        const float tapL = choDelay.popSample (0, t * (float) sampleRate, false);
                        const float tapR = ch > 1 ? choDelay.popSample (1, t * (float) sampleRate, false) : tapL;

                        // The prototype pans the three voices hard left / centre / hard right.
                        const float pan = ((float) v - 1.0f) * 0.7f;
                        sumL += tapL * std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f - pan * 0.5f)) * 1.414f;
                        sumR += tapR * std::sqrt (juce::jlimit (0.0f, 1.0f, 0.5f + pan * 0.5f)) * 1.414f;

                        choPhase[(size_t) v] += (double) (d.choRate * (1.0f + (float) v * 0.24f)) / sampleRate;
                        if (choPhase[(size_t) v] >= 1.0) choPhase[(size_t) v] -= 1.0;
                    }
                    L[i] = dL * dry + sumL * wet * 0.5f;
                    if (ch > 1) R[i] = dR * dry + sumR * wet * 0.5f;
                }
                break;
            }

            case ModuleId::cmb:
            {
                const float wet = wetOf (d.cmbMix), dry = dryOf (d.cmbMix);
                const float fb  = juce::jmin (0.93f, d.cmbFb / 100.0f);   // never >1: it would run away

                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    // Tune is a pitch: the delay is 1/tune seconds. CV scale 0.002s.
                    const float base = 1.0f / juce::jmax (20.0f, d.cmbTune);
                    const float t = juce::jlimit (0.00002f, 0.0499f, base + (hasCV ? cv * 0.002f : 0.0f));
                    const float dsamp = t * (float) sampleRate;

                    for (int i = s; i < s + len; ++i)
                    {
                        const float dL = L[i], dR = R[i];
                        const float tapL = cmbDelay.popSample (0, dsamp, true);
                        const float tapR = ch > 1 ? cmbDelay.popSample (1, dsamp, true) : tapL;
                        cmbDelay.pushSample (0, dL + cmbDampL.processSample (tapL) * fb);
                        if (ch > 1) cmbDelay.pushSample (1, dR + cmbDampR.processSample (tapR) * fb);
                        L[i] = dL * dry + tapL * wet;
                        if (ch > 1) R[i] = dR * dry + tapR * wet;
                    }
                }
                break;
            }

            case ModuleId::fld:
            {
                const float wet = wetOf (d.fldMix), dry = dryOf (d.fldMix);
                const float bias = d.fldSym / 100.0f;

                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    // Drive is drive; CV is a separate multiplicative pre-gain (1 + cv*6),
                    // as the prototype wires it. Folding CV into `amt` was the blunted-effect
                    // bug — it made a patched folder barely respond.
                    const float amt = d.fldDrive / 100.0f;
                    const float preGain = 1.0f + (hasCV ? cv * 6.0f : 0.0f);
                    for (int i = s; i < s + len; ++i)
                    {
                        const float dL = L[i], dR = R[i];
                        L[i] = dL * dry + foldSample (dL, amt, bias, preGain) * wet;
                        if (ch > 1) R[i] = dR * dry + foldSample (dR, amt, bias, preGain) * wet;
                    }
                }
                break;
            }

            case ModuleId::vow:
            {
                const float wet = wetOf (d.vowMix), dry = dryOf (d.vowMix);
                const float q = juce::jmax (2.0f, d.vowSharp);

                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    float f[3];
                    vowelFormantsAt (d.vowMorph, f);
                    for (int k = 0; k < 3; ++k)
                    {
                        // CV scale 1200Hz sweeps the formants — that's what makes it talk.
                        const float ff = juce::jlimit (60.0f, (float) (sampleRate * 0.45),
                                                       f[k] + (hasCV ? cv * 1200.0f : 0.0f));
                        const auto co = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, ff, q);
                        *vowL[(size_t) k].coefficients = co;
                        *vowR[(size_t) k].coefficients = co;
                    }
                    for (int i = s; i < s + len; ++i)
                    {
                        const float dL = L[i], dR = R[i];
                        float wL = 0.0f, wR = 0.0f;
                        for (int k = 0; k < 3; ++k)
                        {
                            wL += vowL[(size_t) k].processSample (dL);
                            if (ch > 1) wR += vowR[(size_t) k].processSample (dR);
                        }
                        L[i] = dL * dry + wL * wet;
                        if (ch > 1) R[i] = dR * dry + wR * wet;
                    }
                }
                break;
            }

            case ModuleId::ech:
            {
                const float wet = wetOf (d.echMix), dry = dryOf (d.echMix);
                const float fb  = juce::jmin (0.92f, d.echFb / 100.0f);   // never >1
                const float wowAmt = d.echWow / 100.0f * 0.004f;

                for (int s = 0; s < n; s += cvChunk)
                {
                    const int len = juce::jmin (cvChunk, n - s);
                    const float wow = std::sin ((float) echWowPhase * juce::MathConstants<float>::twoPi) * wowAmt;
                    const float t = juce::jlimit (0.001f, 1.99f, d.echTime / 1000.0f + wow);
                    const float dsamp = t * (float) sampleRate;

                    for (int i = s; i < s + len; ++i)
                    {
                        const float dL = L[i], dR = R[i];
                        const float tapL = echDelay.popSample (0, dsamp, true);
                        const float tapR = ch > 1 ? echDelay.popSample (1, dsamp, true) : tapL;
                        echDelay.pushSample (0, dL + echDampL.processSample (tapL) * fb);
                        if (ch > 1) echDelay.pushSample (1, dR + echDampR.processSample (tapR) * fb);
                        L[i] = dL * dry + tapL * wet;
                        if (ch > 1) R[i] = dR * dry + tapR * wet;
                    }
                    echWowPhase += 0.37 / sampleRate * len;
                    if (echWowPhase >= 1.0) echWowPhase -= 1.0;
                }
                break;
            }

            default: break;   // src / out / lfo carry no audio processing of their own
        }
    }

    void RackEngine::process(juce::AudioBuffer<float>& buffer, const RackGraph& graph,
                             const RackDials& d) noexcept
    {
        const int n  = buffer.getNumSamples();
        const int ch = buffer.getNumChannels();
        if (n <= 0 || ch <= 0 || n > maxBlock) return;

        // Advance the LFO whether or not it's patched, so its picture keeps moving and a
        // patch made mid-bar lands where the drawing said it would.
        const double lfoInc = (double) juce::jmax (0.01f, d.lfoRate) / sampleRate;
        const float lfoDepth = juce::jlimit (0.0f, 1.0f, d.lfoDepth / 100.0f);
        lastLfoValue = lfoShapeValue (d.lfoShape, lfoPhase) * lfoDepth;
        const float cvAtBlock = lastLfoValue;
        lfoPhase += lfoInc * n;
        if (lfoPhase >= 1.0) lfoPhase -= std::floor (lfoPhase);

        // An unpatched rack must not silence the track: leave the dry beat exactly as it is.
        if (! graph.isLive()) return;

        // processOrder() returns a std::vector — a heap allocation on every block. The
        // patch only changes when you drag a cable, so compute the order into a
        // preallocated array and only when the topology hash says it's stale.
        const auto hash = graph.topologyHash();
        if (hash != cachedTopologyHash)
        {
            cachedOrderLen = graph.processOrderInto (cachedOrder.data(), numRackModules);
            cachedTopologyHash = hash;
        }

        // src's output IS the incoming beat.
        outBuf[(int) ModuleId::src].clear();
        for (int c = 0; c < juce::jmin (2, ch); ++c)
            outBuf[(int) ModuleId::src].copyFrom (c, 0, buffer, c, 0, n);
        if (ch == 1)
            outBuf[(int) ModuleId::src].copyFrom (1, 0, buffer, 0, 0, n);

        // Sum everything that reaches a module's input, then render it.
        auto gatherInto = [&] (ModuleId dest, juce::AudioBuffer<float>& dst)
        {
            dst.clear();
            for (const auto& cable : graph.getCables())
            {
                if (cable.isCV() || cable.to.module != dest) continue;
                const int srcIdx = (int) cable.from.module;
                for (int c = 0; c < 2; ++c)
                    dst.addFrom (c, 0, outBuf[srcIdx], c, 0, n);
            }
        };

        for (int oi = 0; oi < cachedOrderLen; ++oi)
        {
            const auto m = cachedOrder[(size_t) oi];
            auto& mine = outBuf[(int) m];
            gatherInto (m, scratch);
            for (int c = 0; c < 2; ++c) mine.copyFrom (c, 0, scratch, c, 0, n);

            // A bypassed module passes through dry — off means off, not silent.
            if (graph.isBypassed (m)) continue;

            // hasLiveCV, not cvSourcesFor: the latter returns a vector (one allocation per
            // module per block) to answer a question we only ever reduce to a bool.
            const bool hasCV = graph.hasLiveCV (m);

            // Wrapping existing pointers: AudioBuffer uses its 32-slot stack array for
            // channel lists under 32 channels, so this does NOT allocate. (Verified in
            // juce_AudioSampleBuffer.h — "try to avoid doing a malloc here".)
            juce::AudioBuffer<float> view (mine.getArrayOfWritePointers(), 2, n);
            renderModule (m, view, d, cvAtBlock, hasCV);
        }

        // Whatever reaches the main out, at the out level, through the brick wall.
        gatherInto (ModuleId::out, scratch);
        const float lvl = juce::jlimit (0.0f, 1.0f, d.outLvl / 100.0f);
        scratch.applyGain (0, n, lvl);

        {
            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, n);
            juce::dsp::AudioBlock<float> blk (view);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            limiter.process (ctx);
        }

        for (int c = 0; c < ch; ++c)
            buffer.copyFrom (c, 0, scratch, juce::jmin (c, 1), 0, n);
    }
}
