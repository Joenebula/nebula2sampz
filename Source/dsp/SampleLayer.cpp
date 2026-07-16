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
        reset();
    }

    void SampleLayer::reset() noexcept
    {
        for (auto& v : voices) { v.active = false; v.outSample = 0.0; v.outDur = 0.0; }
    }

    void SampleLayer::loadBuffer(juce::AudioBuffer<float>&& audio, double sourceSampleRate,
                                 const juce::String& name, int numSlices)
    {
        auto data = new SampleData();
        data->audio = std::move(audio);
        data->sourceSampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
        data->name = name;

        const int n = data->audio.getNumSamples();
        if (n > 0)
        {
            const auto* ch0 = data->audio.getReadPointer(0);
            const int snapRadius = (int) (0.002 * data->sourceSampleRate);
            data->sliceStarts = computeGridSlices(ch0, n, juce::jmax(1, numSlices), snapRadius);

            // Metadata is a hint; the duration is the evidence.
            const auto tempo = detectTempo((double) n / data->sourceSampleRate, name.toStdString());
            data->detectedBpm = tempo.valid ? (double) tempo.bpm : 0.0;
        }

        SampleData::Ptr held(data);
        retained.push_back(held);          // keep alive: the audio thread may still read the old one
        current.store(data);               // publish
    }

    bool SampleLayer::loadFile(const juce::File& file, int numSlices)
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

        loadBuffer(std::move(buf), reader->sampleRate, file.getFileNameWithoutExtension(), numSlices);
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

    void SampleLayer::noteOn(int note, float velocity) noexcept
    {
        auto* s = current.load();
        if (s == nullptr) return;

        const int numSlices = (int) s->sliceStarts.size() - 1;
        const int slice = note - baseNote;
        if (slice < 0 || slice >= numSlices) return;

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
        v.sliceStart = (double) s->sliceStarts[(size_t) slice];
        v.sliceEnd   = (double) s->sliceStarts[(size_t) slice + 1];
        v.gain       = juce::jlimit(0.0f, 1.0f, velocity);
        v.outSample  = 0.0;
        v.release    = -1.0;
        v.releaseLen = juce::jmax(1.0, 0.005 * hostRate);   // 5 ms

        // Native pitch: 1 output sample advances the source by sourceSR/hostSR.
        v.pitchRate = s->sourceSampleRate / hostRate;

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
        v.hopIn    = v.hopOut * v.pitchRate / juce::jmax(1.0e-6, stretch);

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
        auto* s = current.load();
        if (s == nullptr || numSamples <= 0) return;

        const int srcLen  = s->audio.getNumSamples();
        const int srcChs  = s->audio.getNumChannels();
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
            const auto* src = s->audio.getReadPointer(juce::jmin(chan, srcChs - 1));
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
                    const double read = v.sliceStart + (double) k * v.hopIn + localT * v.pitchRate;

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
