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
    // Note map: B4 (83) plays the WHOLE break; C5 (84) upward plays slice 1, 2, 3...
    // All clear of the GM drum notes (36-46, 75), so the two layers can't fight over a key.
    // Both are gated by note length and both follow the host tempo, so hitting B4 in a
    // 174 BPM session plays a 140 BPM break in time.
    class SampleLayer
    {
    public:
        static constexpr int baseNote = 84;                     // C5 = slice 1, upward
        static constexpr int wholeSampleNote = baseNote - 1;    // B4 = the WHOLE break
        static constexpr int maxVoices = 8;

        SampleLayer();

        void prepare(double hostSampleRate, int maxBlockSize);
        void reset() noexcept;

        // How a loaded break gets divided up.
        struct SliceSettings
        {
            int count = 16;            // grid mode: how many equal chops
            bool transient = false;    // true = slice on detected onsets instead
            float sensitivity = 0.5f;  // transient mode only: higher = more slices
        };

        // Message thread. Decodes, slices and tempo-detects, then publishes.
        bool loadFile(const juce::File& file);

        // Message thread. Same, from an in-memory buffer — no file I/O, so the slicing and
        // playback path is unit-testable.
        void loadBuffer(juce::AudioBuffer<float>&& audio, double sourceSampleRate,
                        const juce::String& name);

        // Message thread: re-slices the SAME audio (no re-decode) and republishes.
        // Allocates — never call from the audio thread.
        void setSliceSettings(const SliceSettings& s);
        SliceSettings getSliceSettings() const noexcept { return slicing; }

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

        // How many superseded SampleData are still held. Exposed for tests: without it,
        // "the leak is fixed" is a claim rather than an observation. Should stay small
        // while audio is running, however many breaks you load.
        int getRetainedCount() const noexcept { return (int) retained.size(); }

        bool hasSample() const noexcept { return current.load() != nullptr; }
        juce::String getSampleName() const;

        // The file this was loaded from, so it can be restored with the project/preset.
        // Empty if the sample came from a buffer rather than disk.
        juce::String getSourcePath() const { return sourcePath; }
        int getNumSlices() const noexcept;
        double getDetectedBpm() const noexcept;
        int activeVoiceCount() const noexcept;

        // --- UI queries (message thread) ---

        // Min/max peaks per horizontal bucket, for drawing the waveform. Returns false if
        // nothing is loaded. Cheap enough to call once per sample change — NOT per frame.
        bool getWaveformPeaks(std::vector<float>& mins, std::vector<float>& maxs, int numBuckets) const;

        // Slice boundaries as 0..1 positions across the sample (numSlices + 1 of them).
        std::vector<float> getSliceBoundariesNormalised() const;

        // Which slices are sounding right now, as a bitmask over slice index (for lighting
        // the playing chop). Read per frame — deliberately cheap.
        uint32_t getPlayingSliceMask() const noexcept;

        // 0..1 progress through the currently-playing slice, or -1 if nothing is playing.
        float getPlayheadNormalised() const noexcept;

    private:
        struct SampleData : public juce::ReferenceCountedObject
        {
            // Shared, so re-slicing publishes new boundaries over the SAME audio instead of
            // copying megabytes per slice-count change.
            std::shared_ptr<const juce::AudioBuffer<float>> audio;
            double sourceSampleRate = 44100.0;
            std::vector<int> sliceStarts;   // numSlices + 1 boundaries
            double detectedBpm = 0.0;
            juce::String name;
            using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;
        };

        void publishSliced(std::shared_ptr<const juce::AudioBuffer<float>> audio,
                           double sourceSampleRate, const juce::String& name, double bpm);

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

            int sliceIndex = -1;
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

        // Superseded SampleData, kept alive until we can PROVE the audio thread has moved
        // on. It used to be a plain vector that only ever grew: since each SampleData holds
        // a shared_ptr to its audio, every break you loaded stayed in memory for the whole
        // session (~3.5MB per 10-second stereo break — 20 of them is 70MB).
        //
        // Reclaiming safely is the whole difficulty: the audio thread reads `current` as a
        // RAW pointer, so a refcount can't tell us whether a render is mid-flight on one.
        // Hence renderCount, which the audio thread bumps once per render. A retired entry
        // is freed only once the count has advanced past the value it had when it was
        // retired — i.e. the audio thread has completed a render that started after the
        // swap, so it cannot still be holding the old pointer. If audio is stopped the
        // count doesn't move and nothing is freed, which is the conservative direction.
        struct Retired
        {
            SampleData::Ptr  data;
            std::uint32_t    retiredAt;   // renderCount when it stopped being current
        };
        std::vector<Retired> retained;              // message thread only
        std::atomic<std::uint32_t> renderCount { 0 };
        void reclaimRetired();                      // message thread only

        std::atomic<SampleData*> current { nullptr };
        std::array<Voice, maxVoices> voices;
        double hostRate = 44100.0;
        double hostBpm = 0.0;
        bool stretchEnabled = true;
        SliceSettings slicing;
        juce::String sourcePath;
    };
}
