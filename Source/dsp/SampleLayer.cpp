#include "SampleLayer.h"
#include "Slicer.h"
#include "TempoDetect.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    SampleLayer::SampleLayer()
    {
        formats.registerBasicFormats();
    }

    void SampleLayer::prepare(double sr, int)
    {
        hostRate = sr;

        juce::dsp::ProcessSpec mono { sr, 512, 1 };
        hauntFiltL.prepare(mono);
        hauntFiltR.prepare(mono);
        // The prototype's haunt tone: a gentle lowpass at 1400 Hz, Q 1.2. Fixed, so the
        // coefficients are built once here — nothing allocates in renderHaunt.
        const auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, 1400.0f, 1.2f);
        *hauntFiltL.coefficients = *c;
        *hauntFiltR.coefficients = *c;

        reset();
    }

    void SampleLayer::reset() noexcept
    {
        for (auto& v : voices) { v.active = false; v.outSample = 0.0; v.outDur = 0.0; }
        hauntFiltL.reset();
        hauntFiltR.reset();
        hauntGain = 0.0f;
        hauntSeen = nullptr;
    }

    int SampleLayer::pickLongestSlice(const SampleData& s) const noexcept
    {
        // The prototype scores by length x tonal-kind x decay; without per-slice analysis
        // the port uses LENGTH alone — the longest slice is the best proxy for sustained,
        // droneable material.
        const int n = (int) s.sliceStarts.size() - 1;
        if (n <= 0) return -1;
        int best = 0, bestLen = 0;
        for (int i = 0; i < n; ++i)
        {
            const int len = s.sliceStarts[(size_t) i + 1] - s.sliceStarts[(size_t) i];
            if (len > bestLen) { bestLen = len; best = i; }
        }
        return best;
    }

    void SampleLayer::renderHaunt(juce::AudioBuffer<float>& bus, int startSample, int numSamples,
                                  float hauntAmt) noexcept
    {
        auto* s = current.load();

        // Target level: the prototype's (haunt/100)*0.42, and it swells in/out slowly.
        const float target = (s != nullptr) ? juce::jlimit(0.0f, 1.0f, hauntAmt / 100.0f) * 0.42f : 0.0f;
        // ~0.6 s smoothing time (the prototype's setTargetAtTime 0.6): per-sample coefficient.
        const float smooth = 1.0f - std::exp(-1.0f / (float) (0.6 * hostRate));

        // Nothing to do if silent and fully faded out.
        if (s == nullptr || (target <= 0.0f && hauntGain < 1.0e-5f)) { hauntGain = 0.0f; return; }

        const int srcLen = s->audio->getNumSamples();
        const int srcChs = s->audio->getNumChannels();
        if (srcLen <= 0 || srcChs <= 0) return;

        // Re-pick the drone slice when the sample (re-slice) changes — cheap, no allocation.
        if (s != hauntSeen)
        {
            const int best = pickLongestSlice(*s);
            if (best < 0) return;
            hauntLoopStart = (double) s->sliceStarts[(size_t) best];
            hauntLoopEnd   = (double) s->sliceStarts[(size_t) best + 1];
            hauntPos = hauntLoopStart;
            hauntSeen = s;
            hauntFiltL.reset();
            hauntFiltR.reset();
        }
        const double loopLen = hauntLoopEnd - hauntLoopStart;
        if (loopLen < 2.0) return;

        // Two octaves down: read the source at 0.25 samples per output sample. (Prototype:
        // playbackRate 0.5 = -1 octave, detune -1200 = another octave.)
        constexpr double rate = 0.25;

        const auto readAt = [&](int chan, double pos) -> float
        {
            const int i0 = (int) pos;
            if (i0 < 0 || i0 >= srcLen) return 0.0f;
            const int i1 = juce::jmin(i0 + 1, srcLen - 1);
            const float frac = (float) (pos - (double) i0);
            const auto* src = s->audio->getReadPointer(juce::jmin(chan, srcChs - 1));
            return src[i0] + frac * (src[i1] - src[i0]);
        };

        const int busChs = bus.getNumChannels();
        auto* outL = bus.getWritePointer(0, startSample);
        auto* outR = busChs > 1 ? bus.getWritePointer(1, startSample) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            hauntGain += (target - hauntGain) * smooth;

            const float l = hauntFiltL.processSample(readAt(0, hauntPos));
            const float r = hauntFiltR.processSample(readAt(1, hauntPos));

            outL[i] += l * hauntGain;
            if (outR != nullptr) outR[i] += r * hauntGain;

            hauntPos += rate;
            if (hauntPos >= hauntLoopEnd) hauntPos -= loopLen;   // loop the slice
        }
    }

    void SampleLayer::publishSliced(std::shared_ptr<const juce::AudioBuffer<float>> audio,
                                    double sourceSampleRate, const juce::String& name, double bpm)
    {
        auto data = new SampleData();
        data->audio = std::move(audio);
        data->sourceSampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
        data->name = name;
        data->detectedBpm = bpm;

        const int n = data->audio != nullptr ? data->audio->getNumSamples() : 0;
        if (n > 0)
        {
            const auto* ch0 = data->audio->getReadPointer(0);
            const int snapRadius = (int) (0.002 * data->sourceSampleRate);

            if (slicing.transient)
            {
                // Slice where the drums actually hit, not on a metronome.
                data->sliceStarts = detectTransients(ch0, n, data->sourceSampleRate, slicing.sensitivity);
            }
            else
            {
                data->sliceStarts = computeGridSlices(ch0, n, juce::jmax(1, slicing.count), snapRadius);
            }
        }

        // Stamp the OUTGOING entry with the render count as of now, BEFORE the swap, so the
        // stamp can never be newer than the moment it stopped being current.
        const auto stamp = renderCount.load();

        SampleData::Ptr held(data);
        retained.push_back({ held, 0xffffffffu });   // the incoming one: never reclaimable
                                                     // while it's current (reclaim skips it)
        current.store(data);               // publish

        // Now the previous entries are all superseded: give any that were still unstamped
        // this swap's stamp, and reclaim whatever the audio thread has provably finished.
        for (auto& r : retained)
            if (r.retiredAt == 0xffffffffu && r.data.get() != data)
                r.retiredAt = stamp;

        reclaimRetired();
    }

    void SampleLayer::reclaimRetired()
    {
        const auto now = renderCount.load();
        auto* cur = current.load();

        for (auto it = retained.begin(); it != retained.end();)
        {
            const bool isCurrent = it->data.get() == cur;
            // Free only once a render has STARTED AND FINISHED since this entry was
            // retired. A render in flight when we swapped had already loaded the old
            // pointer; by the time the count has moved past the stamp, that render has
            // returned. If audio is stopped the count is frozen and we free nothing —
            // conservative, and memory comes back on the next publish once it resumes.
            const bool settled = ! isCurrent
                              && it->retiredAt != 0xffffffffu
                              && now > it->retiredAt + 1;
            it = settled ? retained.erase(it) : it + 1;
        }
    }

    void SampleLayer::loadBuffer(juce::AudioBuffer<float>&& audio, double sourceSampleRate,
                                 const juce::String& name)
    {
        auto shared = std::make_shared<const juce::AudioBuffer<float>>(std::move(audio));
        const int n = shared->getNumSamples();

        // Metadata is a hint; the duration is the evidence.
        double bpm = 0.0;
        if (n > 0)
        {
            const auto tempo = detectTempo((double) n / (sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0),
                                           name.toStdString());
            bpm = tempo.valid ? (double) tempo.bpm : 0.0;
        }

        publishSliced(std::move(shared), sourceSampleRate, name, bpm);
    }

    void SampleLayer::setSliceSettings(const SliceSettings& s)
    {
        slicing = s;

        // Re-slice the SAME audio — no re-decode, no copy.
        auto* cur = current.load();
        if (cur == nullptr || cur->audio == nullptr) return;
        publishSliced(cur->audio, cur->sourceSampleRate, cur->name, cur->detectedBpm);
    }

    bool SampleLayer::loadFile(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr) return false;

        const int len = (int) reader->lengthInSamples;
        if (len <= 0) return false;

        juce::AudioBuffer<float> buf((int) juce::jmax(1u, reader->numChannels), len);
        reader->read(&buf, 0, len, 0, true, true);

        // Mono files: duplicate so downstream can always assume stereo-ish access.
        if (buf.getNumChannels() == 1)
        {
            juce::AudioBuffer<float> stereo(2, len);
            stereo.copyFrom(0, 0, buf, 0, 0, len);
            stereo.copyFrom(1, 0, buf, 0, 0, len);
            buf = std::move(stereo);
        }

        loadBuffer(std::move(buf), reader->sampleRate, file.getFileNameWithoutExtension());
        sourcePath = file.getFullPathName();   // so the project can restore it
        return true;
    }

    juce::String SampleLayer::getSampleName() const
    {
        auto* s = current.load();
        return s != nullptr ? s->name : juce::String();
    }

    int SampleLayer::getNumSlices() const noexcept
    {
        auto* s = current.load();
        return (s != nullptr && s->sliceStarts.size() > 1) ? (int) s->sliceStarts.size() - 1 : 0;
    }

    double SampleLayer::getDetectedBpm() const noexcept
    {
        auto* s = current.load();
        return s != nullptr ? s->detectedBpm : 0.0;
    }

    int SampleLayer::activeVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices) if (v.active) ++n;
        return n;
    }

    bool SampleLayer::getWaveformPeaks(std::vector<float>& mins, std::vector<float>& maxs, int numBuckets) const
    {
        auto* s = current.load();
        if (s == nullptr || numBuckets <= 0) return false;

        const int n = s->audio->getNumSamples();
        if (n <= 0) return false;

        mins.assign((size_t) numBuckets, 0.0f);
        maxs.assign((size_t) numBuckets, 0.0f);

        const auto* d = s->audio->getReadPointer(0);
        for (int b = 0; b < numBuckets; ++b)
        {
            const int i0 = (int) ((int64_t) b * n / numBuckets);
            const int i1 = juce::jmax(i0 + 1, (int) ((int64_t) (b + 1) * n / numBuckets));
            float lo = 0.0f, hi = 0.0f;
            for (int i = i0; i < i1 && i < n; ++i) { lo = juce::jmin(lo, d[i]); hi = juce::jmax(hi, d[i]); }
            mins[(size_t) b] = lo;
            maxs[(size_t) b] = hi;
        }
        return true;
    }

    std::vector<float> SampleLayer::getSliceBoundariesNormalised() const
    {
        std::vector<float> out;
        auto* s = current.load();
        if (s == nullptr) return out;

        const int n = s->audio->getNumSamples();
        if (n <= 0) return out;

        out.reserve(s->sliceStarts.size());
        for (const int b : s->sliceStarts)
            out.push_back(juce::jlimit(0.0f, 1.0f, (float) b / (float) n));
        return out;
    }

    uint32_t SampleLayer::getPlayingSliceMask() const noexcept
    {
        uint32_t mask = 0;
        for (const auto& v : voices)
            if (v.active && v.sliceIndex >= 0 && v.sliceIndex < 32)
                mask |= (1u << v.sliceIndex);
        return mask;
    }

    float SampleLayer::getPlayheadNormalised() const noexcept
    {
        auto* s = current.load();
        if (s == nullptr) return -1.0f;
        const int n = s->audio->getNumSamples();
        if (n <= 0) return -1.0f;

        // The newest-sounding voice wins (the chop you just hit).
        const Voice* best = nullptr;
        for (const auto& v : voices)
            if (v.active && (best == nullptr || v.outSample < best->outSample)) best = &v;
        if (best == nullptr) return -1.0f;

        // How far through the slice are we, in INPUT samples?
        const double progressed = best->outSample * (best->outDur > 0.0
                                     ? (best->sliceEnd - best->sliceStart) / best->outDur : 0.0);
        return juce::jlimit(0.0f, 1.0f, (float) ((best->sliceStart + progressed) / (double) n));
    }

    void SampleLayer::noteOn(int note, float velocity) noexcept
    {
        auto* s = current.load();
        if (s == nullptr) return;

        const int numSlices = (int) s->sliceStarts.size() - 1;
        const int slice = note - baseNote;
        const bool whole = (note == wholeSampleNote);
        if (! whole && (slice < 0 || slice >= numSlices)) return;

        // Free voice, else steal the most-advanced one.
        int slot = -1;
        for (int i = 0; i < maxVoices; ++i) if (! voices[(size_t) i].active) { slot = i; break; }
        if (slot < 0)
        {
            double best = -1.0;
            for (int i = 0; i < maxVoices; ++i)
                if (voices[(size_t) i].outSample > best) { best = voices[(size_t) i].outSample; slot = i; }
        }

        auto& v = voices[(size_t) slot];
        v.note       = note;
        v.sliceIndex = whole ? -1 : slice;
        v.sliceStart = whole ? 0.0 : (double) s->sliceStarts[(size_t) slice];
        v.sliceEnd   = whole ? (double) s->audio->getNumSamples()
                             : (double) s->sliceStarts[(size_t) slice + 1];
        v.gain       = juce::jlimit(0.0f, 1.0f, velocity);
        v.outSample  = 0.0;
        v.release    = -1.0;
        v.releaseLen = juce::jmax(1.0, 0.005 * hostRate);   // 5 ms

        // Native pitch: 1 output sample advances the source by sourceSR/hostSR.
        const double nativeRate = s->sourceSampleRate / hostRate;

        // Transpose (grid Pitch +/-). Only the grain READ speed moves; hopIn below is
        // computed from nativeRate, so the chop's LENGTH is untouched. Exactly 1.0 when
        // the offset is 0, so an unpainted lane is bit-for-bit the old behaviour.
        const double transpose = pitchOffsetSemis == 0.0f
                               ? 1.0
                               : std::pow(2.0, (double) pitchOffsetSemis / 12.0);
        v.nativeRate = nativeRate;
        v.pitchRate  = nativeRate * transpose;

        const double inSamples = juce::jmax(1.0, v.sliceEnd - v.sliceStart);
        const double inSeconds = inSamples / s->sourceSampleRate;

        // stretch = outDur/inDur. Fitting a nativeBpm loop into a hostBpm session means
        // playing it at native/host. Unknown tempo (or stretch off) = play as-is.
        double stretch = 1.0;
        if (stretchEnabled && s->detectedBpm > 0.0 && hostBpm > 0.0)
            stretch = juce::jlimit(0.25, 4.0, s->detectedBpm / hostBpm);

        const double outSeconds = inSeconds * stretch;
        v.outDur = outSeconds * hostRate;

        // 35-90 ms grains, 50% overlap (the prototype's numbers).
        const double grainSeconds = juce::jlimit(0.035, 0.09, inSeconds * 0.35);
        v.grainOut = juce::jmax(8.0, grainSeconds * hostRate);
        v.hopOut   = v.grainOut * 0.5;
        // Input hop: how far through the source each successive grain starts. This is the
        // only thing that changes with stretch — the grains themselves stay at native pitch.
        v.hopIn    = v.hopOut * nativeRate / juce::jmax(1.0e-6, stretch);

        v.active = true;
    }

    void SampleLayer::noteOff(int note) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.release < 0.0)
                v.release = v.releaseLen;      // start the 5 ms fade
    }

    void SampleLayer::render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept
    {
        // Tells the message thread this render has begun, so it knows which retired
        // SampleData can no longer be in flight. Bumped unconditionally and first — an
        // early return still counts as "this render is done with the old pointer".
        renderCount.fetch_add(1, std::memory_order_release);

        auto* s = current.load();
        if (s == nullptr || numSamples <= 0) return;

        const int srcLen  = s->audio->getNumSamples();
        const int srcChs  = s->audio->getNumChannels();
        const int busChs  = bus.getNumChannels();
        if (srcLen <= 0 || srcChs <= 0) return;

        // Reads the source with linear interpolation (a 44.1k file in a 48k session must
        // not run sharp).
        const auto readAt = [&](int chan, double pos) -> float
        {
            const int i0 = (int) pos;
            if (i0 < 0 || i0 >= srcLen) return 0.0f;
            const int i1 = juce::jmin(i0 + 1, srcLen - 1);
            const float frac = (float) (pos - (double) i0);
            const auto* src = s->audio->getReadPointer(juce::jmin(chan, srcChs - 1));
            return src[i0] + frac * (src[i1] - src[i0]);
        };

        for (auto& v : voices)
        {
            if (! v.active) continue;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.outSample >= v.outDur) { v.active = false; break; }

                // 50% overlap means at most two grains are sounding at any instant.
                const int k0 = (int) std::floor(v.outSample / v.hopOut);
                float acc[2] = { 0.0f, 0.0f };

                for (int k = juce::jmax(0, k0 - 1); k <= k0; ++k)
                {
                    const double localT = v.outSample - (double) k * v.hopOut;
                    if (localT < 0.0 || localT >= v.grainOut) continue;

                    // Triangular window: up to the midpoint, back down. Summed at 50%
                    // overlap this reconstructs unity, so grains don't pump.
                    //
                    // EXCEPT the first grain, which does NOT fade in. Nothing overlaps it
                    // yet, so a rising window would ramp the chop up from silence over
                    // ~45ms — softening the transient, which on a breakbeat slicer is the
                    // whole sound. (The prototype's playStretched ramps its first grain
                    // from ~0 and has exactly this problem in stretch mode.) Holding it at
                    // full keeps the attack AND still sums to unity from t=0.
                    const double half = v.grainOut * 0.5;
                    const bool risingEdge = localT < half;
                    const float w = (k == 0 && risingEdge)
                                      ? 1.0f
                                      : (float) (risingEdge ? localT / half
                                                            : (v.grainOut - localT) / half);

                    // Read straight through. Do NOT wrap back to the slice start: the
                    // source is one continuous break, so a grain reading slightly past a
                    // slice edge is just the next moment of the same recording — exactly
                    // what should follow. Wrapping instead jumped mid-grain back to the
                    // chop's beginning, so the last chunk of every slice never played.
                    // readAt() returns 0 outside the buffer, so the end is safe.
                    //
                    // Anchored at the grain's CENTRE, not its start. Reading a transposed
                    // grain from its start drifts the whole grain forward by up to its own
                    // length (~45 ms), which moves the chop in time — a landmark burst came
                    // out 35 ms early at +12. Pivoting about the centre makes that error
                    // symmetric within the grain so it cancels across the overlap, and the
                    // chop stays put. Identical to reading from the start when pitchRate ==
                    // nativeRate, so an untransposed voice is bit-for-bit unchanged.
                    const double read = v.sliceStart + (double) k * v.hopIn
                                      + half * v.nativeRate + (localT - half) * v.pitchRate;

                    for (int c = 0; c < busChs && c < 2; ++c)
                        acc[c] += readAt(c, read) * w;
                }

                // Note-off gate: 5 ms fade, then the voice frees itself.
                float env = v.gain;
                if (v.release >= 0.0)
                {
                    env *= (float) (v.release / v.releaseLen);
                    v.release -= 1.0;
                    if (v.release <= 0.0) { v.active = false; }
                }

                for (int c = 0; c < busChs && c < 2; ++c)
                    bus.addSample(c, startSample + i, acc[c] * env);

                v.outSample += 1.0;
                if (! v.active) break;
            }
        }
    }
}
