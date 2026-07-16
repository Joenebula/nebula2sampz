#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <vector>
#include <array>

namespace Nebula2
{
    // The chopped-sample layer: load a break, slice it, play the slices from MIDI.
    //
    // Threading: loading decodes a file and allocates, so it happens on the MESSAGE thread.
    // It builds an immutable SampleData and publishes it via an atomic pointer; the audio
    // thread only ever reads that pointer. Superseded data is retained (never freed while
    // the audio thread might still be reading it).
    //
    // Slices are mapped from note 84 (C5) upward, clear of the GM drum notes (36-46, 75),
    // so the two layers can't fight over a key.
    class SampleLayer
    {
    public:
        static constexpr int baseNote = 84;
        static constexpr int maxVoices = 8;

        SampleLayer();

        void prepare(double hostSampleRate, int maxBlockSize);
        void reset() noexcept;

        // Message thread. Decodes, slices and tempo-detects, then publishes.
        bool loadFile(const juce::File& file, int numSlices);

        // Message thread. Same, from an in-memory buffer — no file I/O, so the slicing and
        // playback path is unit-testable.
        void loadBuffer(juce::AudioBuffer<float>&& audio, double sourceSampleRate,
                        const juce::String& name, int numSlices);

        void noteOn(int midiNoteNumber, float velocity) noexcept;
        void render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept;

        bool hasSample() const noexcept { return current.load() != nullptr; }
        juce::String getSampleName() const;
        int getNumSlices() const noexcept;
        double getDetectedBpm() const noexcept;
        int activeVoiceCount() const noexcept;

    private:
        struct SampleData : public juce::ReferenceCountedObject
        {
            juce::AudioBuffer<float> audio;
            double sourceSampleRate = 44100.0;
            std::vector<int> sliceStarts;   // numSlices + 1 boundaries
            double detectedBpm = 0.0;
            juce::String name;
            using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;
        };

        struct Voice
        {
            double pos = 0.0;      // fractional read position (resampling)
            double end = 0.0;
            float gain = 1.0f;
            bool active = false;
        };

        juce::AudioFormatManager formats;
        std::vector<SampleData::Ptr> retained;      // message thread only
        std::atomic<SampleData*> current { nullptr };
        std::array<Voice, maxVoices> voices;
        double hostRate = 44100.0;
    };
}
