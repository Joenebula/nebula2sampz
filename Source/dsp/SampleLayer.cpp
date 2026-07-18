#include "SampleLayer.h"
#include "Slicer.h"
#include "TempoDetect.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    std::vector<int> shuffledSliceOrder(int count, juce::Random& rng)
    {
        const int n = juce::jlimit(0, SampleLayer::maxSlices, count);
        std::vector<int> order((size_t) n);
        for (int i = 0; i < n; ++i) order[(size_t) i] = i;

        // Fisher-Yates: a PERMUTATION, not n independent picks. Picking independently would
        // let one slice play twice while another never played at all — a different sound
        // from "the same break rearranged", and not what anybody means by shuffle.
        for (int i = n - 1; i > 0; --i)
            std::swap(order[(size_t) i], order[(size_t) rng.nextInt(i + 1)]);

        return order;
    }

    SampleLayer::SampleLayer()
    {
        formats.registerBasicFormats();
        resetSliceOrder();
        // std::atomic members are NOT zero-initialised. Without this every slice gain was
        // indeterminate, which in practice meant 0 — the whole sampler went silent and
        // twelve unrelated tests failed at once.
        resetSliceSettings();
    }

    void SampleLayer::resetSliceOrder() noexcept
    {
        for (int i = 0; i < maxSlices; ++i) sliceOrder[(size_t) i].store(i);
    }

    void SampleLayer::setSliceOrder(const std::vector<int>& order) noexcept
    {
        for (int i = 0; i < maxSlices; ++i)
        {
            const int v = i < (int) order.size() ? order[(size_t) i] : i;
            // An out-of-range entry falls back to IDENTITY, not to 0: a corrupt or stale
            // map should degrade to "this pad plays itself", not "everything plays slice 0".
            sliceOrder[(size_t) i].store((v >= 0 && v < maxSlices) ? v : i);
        }
    }

    std::vector<SliceInfo> SampleLayer::analyseCurrentSlices() const
    {
        auto* s = current.load();
        if (s == nullptr || s->audio == nullptr) return {};
        return Nebula2::analyseSlices(*s->audio, s->sliceStarts, s->sourceSampleRate);
    }

    std::vector<int> SampleLayer::getSliceOrder() const
    {
        std::vector<int> out((size_t) maxSlices);
        for (int i = 0; i < maxSlices; ++i) out[(size_t) i] = sliceOrder[(size_t) i].load();
        return out;
    }

    int SampleLayer::sliceForPad(int pad) const noexcept
    {
        if (pad < 0 || pad >= maxSlices) return pad;
        return sliceOrder[(size_t) pad].load();
    }

    void SampleLayer::resetSliceSettings() noexcept
    {
        for (int i = 0; i < maxSlices; ++i)
        {
            sliceGain[(size_t) i].store(1.0f);
            slicePan[(size_t) i].store(0.0f);
            sliceSemis[(size_t) i].store(0.0f);
            sliceRev[(size_t) i].store(false);
        }
    }

    void SampleLayer::setSliceGain(int slice, float g) noexcept
    {
        if (slice < 0 || slice >= maxSlices) return;
        sliceGain[(size_t) slice].store(juce::jlimit(0.0f, 1.5f, g));
    }

    void SampleLayer::setSlicePan(int slice, float p) noexcept
    {
        if (slice < 0 || slice >= maxSlices) return;
        slicePan[(size_t) slice].store(juce::jlimit(-1.0f, 1.0f, p));
    }

    void SampleLayer::setSliceSemitones(int slice, float s) noexcept
    {
        if (slice < 0 || slice >= maxSlices) return;
        sliceSemis[(size_t) slice].store(juce::jlimit(-24.0f, 24.0f, s));
    }

    void SampleLayer::setSliceReverse(int slice, bool r) noexcept
    {
        if (slice < 0 || slice >= maxSlices) return;
        sliceRev[(size_t) slice].store(r);
    }

    SampleLayer::SliceSettingsView SampleLayer::getSliceSettings(int slice) const noexcept
    {
        if (slice < 0 || slice >= maxSlices) return { 1.0f, 0.0f, 0.0f, false };
        return { sliceGain[(size_t) slice].load(),
                 slicePan[(size_t) slice].load(),
                 sliceSemis[(size_t) slice].load(),
                 sliceRev[(size_t) slice].load() };
    }

    juce::String SampleLayer::sliceSettingsToString() const
    {
        // One slice per token: gain|pan|semis|rev
        juce::String out;
        for (int i = 0; i < maxSlices; ++i)
        {
            if (i > 0) out << ",";
            const auto v = getSliceSettings(i);
            out << juce::String(v.gain, 3) << "|" << juce::String(v.pan, 3) << "|"
                << juce::String(v.semitones, 2) << "|" << (v.reverse ? 1 : 0);
        }
        return out;
    }

    void SampleLayer::sliceSettingsFromString(const juce::String& s) noexcept
    {
        if (s.isEmpty()) { resetSliceSettings(); return; }

        auto rows = juce::StringArray::fromTokens(s, ",", "");
        // Same rule as the order map: garbage means "I don't know what these were", and the
        // honest answer to that is the neutral setting, not a half-applied mixture.
        for (const auto& row : rows)
            if (juce::StringArray::fromTokens(row, "|", "").size() != 4)
            {
                resetSliceSettings();
                return;
            }

        resetSliceSettings();
        for (int i = 0; i < juce::jmin(rows.size(), maxSlices); ++i)
        {
            auto f = juce::StringArray::fromTokens(rows[i], "|", "");
            setSliceGain(i, f[0].getFloatValue());
            setSlicePan(i, f[1].getFloatValue());
            setSliceSemitones(i, f[2].getFloatValue());
            setSliceReverse(i, f[3].getIntValue() != 0);
        }
    }

    bool SampleLayer::orderIsRearranged() const noexcept
    {
        for (int i = 0; i < maxSlices; ++i)
            if (sliceOrder[(size_t) i].load() != i) return true;
        return false;
    }

    void SampleLayer::startSeqSlice(const SampleData& s, int pad) noexcept
    {
        const int numSlices = (int) s.sliceStarts.size() - 1;
        if (numSlices <= 0 || pad < 0 || pad >= numSlices) { seqVoice = -1; return; }

        int slice = sliceForPad(pad);
        if (slice < 0 || slice >= numSlices) slice = pad;

        // Always voice 0: the sequence is ONE continuous performance, so each slice must
        // hand over to the next in the same slot rather than stacking eight voices deep.
        auto& v = voices[0];
        v.note       = wholeSampleNote;
        v.sliceIndex = slice;
        v.sliceStart = (double) s.sliceStarts[(size_t) slice];
        v.sliceEnd   = (double) s.sliceStarts[(size_t) slice + 1];
        {
            // Per-slice shaping. Unity at centre — see the note in noteOn.
            const auto ss = getSliceSettings(slice);
            v.gain  = seqGain * ss.gain;
            v.panL  = ss.pan <= 0.0f ? 1.0f : 1.0f - ss.pan;
            v.panR  = ss.pan >= 0.0f ? 1.0f : 1.0f + ss.pan;
            v.reverse = ss.reverse;
            v.sliceSemis = ss.semitones;
        }
        v.outSample  = 0.0;
        v.release    = -1.0;
        v.releaseLen = juce::jmax(1.0, 0.005 * hostRate);

        const double nativeRate = s.sourceSampleRate / hostRate;
        const float totalSemis = pitchOffsetSemis + v.sliceSemis;
        const double transpose = totalSemis == 0.0f
                               ? 1.0 : std::pow(2.0, (double) totalSemis / 12.0);
        v.nativeRate = nativeRate;
        v.pitchRate  = nativeRate * transpose;

        const double inSamples = juce::jmax(1.0, v.sliceEnd - v.sliceStart);
        const double inSeconds = inSamples / s.sourceSampleRate;

        double stretch = 1.0;
        if (stretchEnabled && s.detectedBpm > 0.0 && hostBpm > 0.0)
            stretch = juce::jlimit(0.25, 4.0, s.detectedBpm / hostBpm);

        v.outDur = inSeconds * stretch * hostRate;

        const double grainSeconds = juce::jlimit(0.035, 0.09, inSeconds * 0.35);
        v.grainOut = juce::jmax(8.0, grainSeconds * hostRate);
        v.hopOut   = v.grainOut * 0.5;
        v.hopIn    = v.hopOut * nativeRate / juce::jmax(1.0e-6, stretch);

        v.active = true;
        seqVoice = 0;
        seqPad = pad;
    }

    juce::String SampleLayer::sliceOrderToString() const
    {
        juce::String out;
        for (int i = 0; i < maxSlices; ++i)
        {
            if (i > 0) out << ",";
            out << sliceOrder[(size_t) i].load();
        }
        return out;
    }

    void SampleLayer::sliceOrderFromString(const juce::String& s) noexcept
    {
        if (s.isEmpty()) { resetSliceOrder(); return; }

        auto toks = juce::StringArray::fromTokens(s, ",", "");
        std::vector<int> order;
        order.reserve((size_t) toks.size());

        for (const auto& t : toks)
        {
            // Reject the WHOLE string if any entry isn't a number, rather than taking
            // getIntValue()'s silent 0. Non-numeric text scores 0, which is in range, so it
            // sails past the identity fallback in setSliceOrder and quietly turns the break
            // into slice 0 played over and over — the exact failure that fallback exists to
            // stop, arriving by a different road. Corrupt data means "I don't know the
            // arrangement", and the honest answer to that is the original order.
            const auto trimmed = t.trim();
            if (trimmed.isEmpty() || ! trimmed.containsOnly("-0123456789"))
            {
                resetSliceOrder();
                return;
            }
            order.push_back(trimmed.getIntValue());
        }

        setSliceOrder(order);
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
        const bool whole = (note == wholeSampleNote);

        // The note picks a PAD; the pad's order entry picks the slice. Identity unless the
        // break has been rearranged, so an untouched instrument plays exactly as before.
        const int pad = note - baseNote;
        if (! whole && (pad < 0 || pad >= numSlices)) return;

        int slice = whole ? 0 : sliceForPad(pad);
        // The order map is `maxSlices` long regardless of how many slices the break
        // currently has, so an entry can point past the end after a re-slice to a smaller
        // count. Fall back to the pad rather than dropping the note.
        if (! whole && (slice < 0 || slice >= numSlices)) slice = pad;

        // The whole-break note on a REARRANGED break plays the slices in their new order,
        // one after another, instead of streaming the raw file. This is the path the in-app
        // Play button uses, and without it a shuffle moved the numbers on screen and
        // changed nothing you could hear.
        if (whole && orderIsRearranged() && numSlices > 0)
        {
            seqGain = juce::jlimit(0.0f, 1.0f, velocity);
            for (auto& vv : voices) vv.active = false;    // one performance, not a pile-up
            startSeqSlice(*s, 0);
            return;
        }
        seqVoice = -1;

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
        {
            // Per-slice shaping (skipped for the whole-break note, which isn't one slice).
            const auto ss = whole ? SliceSettingsView{ 1.0f, 0.0f, 0.0f, false }
                                  : getSliceSettings(slice);
            v.gain *= ss.gain;
            // Unity at CENTRE, not equal-power. Equal-power puts the centre at -3 dB, which
            // would quietly drop the level of every existing sound the moment this shipped —
            // a pan control is not allowed to change how loud a break is when it is centred.
            v.panL = ss.pan <= 0.0f ? 1.0f : 1.0f - ss.pan;
            v.panR = ss.pan >= 0.0f ? 1.0f : 1.0f + ss.pan;
            v.reverse = ss.reverse;
            v.sliceSemis = ss.semitones;
        }
        v.outSample  = 0.0;
        v.release    = -1.0;
        v.releaseLen = juce::jmax(1.0, 0.005 * hostRate);   // 5 ms

        // Native pitch: 1 output sample advances the source by sourceSR/hostSR.
        const double nativeRate = s->sourceSampleRate / hostRate;

        // Transpose (grid Pitch +/-). Only the grain READ speed moves; hopIn below is
        // computed from nativeRate, so the chop's LENGTH is untouched. Exactly 1.0 when
        // the offset is 0, so an unpainted lane is bit-for-bit the old behaviour.
        // The grid lane's transpose AND this slice's own, added in semitones (which is
        // where they compose — multiplying two ratios is the same thing, adding the
        // semitones just says so).
        const float totalSemis = pitchOffsetSemis + v.sliceSemis;
        const double transpose = totalSemis == 0.0f
                               ? 1.0
                               : std::pow(2.0, (double) totalSemis / 12.0);
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
        // Lifting the whole-break key ends the SEQUENCE too, or it would keep advancing
        // through the remaining slices with nothing holding it down.
        if (note == wholeSampleNote) seqVoice = -1;

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
                if (v.outSample >= v.outDur)
                {
                    v.active = false;

                    // Sequenced whole-break playback: this slice is done, so hand straight
                    // over to the next PAD. Without this the break would stop dead after
                    // its first chop instead of playing the rearrangement through.
                    if (seqVoice == 0 && (&v == &voices[0]))
                    {
                        const int numSlices = (int) s->sliceStarts.size() - 1;
                        if (seqPad + 1 < numSlices)
                        {
                            startSeqSlice(*s, seqPad + 1);
                            // Carry on filling this block from where the last slice ended,
                            // or every handover would leave a hole the length of whatever
                            // remained of the block.
                            continue;
                        }
                        seqVoice = -1;      // end of the break
                    }
                    break;
                }

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

                    // REVERSE mirrors the read inside the slice, so the chop plays back to
                    // front while still starting and ending on its own boundaries — the
                    // grain machinery and the timing are untouched.
                    const double rd = v.reverse ? (v.sliceStart + v.sliceEnd - read) : read;

                    for (int c = 0; c < busChs && c < 2; ++c)
                        acc[c] += readAt(c, rd) * w;
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
                    bus.addSample(c, startSample + i, acc[c] * env * (c == 0 ? v.panL : v.panR));

                v.outSample += 1.0;
                if (! v.active) break;
            }
        }
    }
}
