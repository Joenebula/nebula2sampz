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
        for (auto& v : voices) { v.active = false; v.pos = 0.0; v.end = 0.0; }
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
                if (voices[(size_t) i].pos > best) { best = voices[(size_t) i].pos; slot = i; }
        }

        auto& v = voices[(size_t) slot];
        v.pos    = (double) s->sliceStarts[(size_t) slice];
        v.end    = (double) s->sliceStarts[(size_t) slice + 1];
        v.gain   = juce::jlimit(0.0f, 1.0f, velocity);
        v.active = true;
    }

    void SampleLayer::render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept
    {
        auto* s = current.load();
        if (s == nullptr || numSamples <= 0) return;

        const int srcLen  = s->audio.getNumSamples();
        const int srcChs  = s->audio.getNumChannels();
        const int busChs  = bus.getNumChannels();
        if (srcLen <= 0 || srcChs <= 0) return;

        // Play at the sample's own pitch regardless of session rate (44.1k file in a 48k
        // session must not run sharp). Tempo-matching comes with the OLA stretch.
        const double rate = s->sourceSampleRate / hostRate;

        for (auto& v : voices)
        {
            if (! v.active) continue;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.pos >= v.end || v.pos >= (double) (srcLen - 1)) { v.active = false; break; }

                const int i0 = (int) v.pos;
                const int i1 = juce::jmin(i0 + 1, srcLen - 1);
                const float frac = (float) (v.pos - (double) i0);

                for (int c = 0; c < busChs; ++c)
                {
                    const auto* src = s->audio.getReadPointer(juce::jmin(c, srcChs - 1));
                    const float sample = src[i0] + frac * (src[i1] - src[i0]);   // linear interp
                    bus.addSample(c, startSample + i, sample * v.gain);
                }
                v.pos += rate;
            }
        }
    }
}
