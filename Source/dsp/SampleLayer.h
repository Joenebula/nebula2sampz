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

        // Gates the chop to the note, like a slicer should: hold a 16th, get a 16th-long
        // chop. Without this, every slice runs its full length and a fast pattern turns
        // into overlapping mush (and steals voices from itself).
        void noteOff(int midiNoteNumber) noexcept;

        void render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept;

        // Host tempo, so slices can be stretched to fit the grid. 0 = unknown (no stretch).
        void setHostBpm(double bpm) noexcept { hostBpm = bpm; }

        // Time-stretch on/off. Off = classic repitch (slice plays at its own speed).
        void setStretchEnabled(bool shouldStretch) noexcept { stretchEnabled = shouldStretch; }

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

        // Granular OLA voice (the prototype's playStretched, ported):
        // grains are read at native pitch; the SPACING between grain start points is what
        // compresses or expands time. So a 136 BPM break fits a 174 BPM session without
        // pitching up. stretch = outDur/inDur (= nativeBpm/hostBpm); 1.0 = no stretch.
        struct Voice
        {
            bool active = false;
            int note = -1;
            double sliceStart = 0.0, sliceEnd = 0.0;
            float gain = 1.0f;

            double outSample = 0.0;     // output samples since note-on
            double outDur = 0.0;        // total output samples for this slice
            double grainOut = 0.0;      // grain length, in OUTPUT samples
            double hopOut = 0.0;        // grain spacing out (= grainOut/2, 50% overlap)
            double hopIn = 0.0;         // grain spacing in INPUT samples
            double pitchRate = 1.0;     // input samples per output sample (native pitch)

            // Short fade on note-off so gating a chop doesn't click.
            double release = -1.0;      // samples remaining; < 0 = not releasing
            double releaseLen = 1.0;
        };

        juce::AudioFormatManager formats;
        std::vector<SampleData::Ptr> retained;      // message thread only
        std::atomic<SampleData*> current { nullptr };
        std::array<Voice, maxVoices> voices;
        double hostRate = 44100.0;
        double hostBpm = 0.0;
        bool stretchEnabled = true;
    };
}
