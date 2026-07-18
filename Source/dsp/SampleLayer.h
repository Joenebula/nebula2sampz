#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "SliceAnalysis.h"
#include "AmpShape.h"
#include <juce_dsp/juce_dsp.h>
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
    // Note map: C3 (60) plays the WHOLE break (the loop); C#3 (61) upward plays slice 1, 2,
    // 3... C3 is the root because the loop is what you reach for first.
    //
    // 60 is C3 in Cubase, Ableton and Logic. Note NAMES are a naming convention, not part of
    // MIDI, and the other one calls 60 "C4" — so "C3" alone doesn't specify a note. Getting
    // that backwards is what put the loop an octave below the key the user pressed.
    //
    // EVERY OTHER KEY PLAYS SOMETHING. Notes outside the slice range wrap, so there are no
    // dead keys — you can draw a note anywhere on the piano roll and hear a chop.
    //
    // It used to start at C5, and only because notes 36-46 were reserved for a synth-drum
    // layer. That layer was deleted; the reservation outlived it, so a note drawn where any
    // musician would draw one produced silence. The constraint was gone and the map wasn't.
    //
    // Gated by note length and following the host tempo, so hitting the root in a 174 BPM
    // session plays a 140 BPM break in time.
    // A shuffled pad->slice map for `count` slices: a permutation, so every slice still
    // plays exactly once and nothing is lost or doubled. Free function and RNG-by-reference
    // so a seed reproduces an arrangement, which is the only way to test a shuffle.
    std::vector<int> shuffledSliceOrder(int count, juce::Random& rng);

    class SampleLayer
    {
    public:
        // C3 is the LOOP — the whole break — because that is the thing you reach for first.
        // The slices climb from just above it, so the root and its chops sit together
        // rather than the loop hiding a semitone below where you would look for it.
        //
        // 60, not 48. Cubase, Ableton and Logic all name MIDI note 60 "C3" (middle C = C3).
        // The other convention calls 60 "C4", and taking it put the loop an octave below
        // where the user's own DAW said C3 was: playing C3 in Cubase sent note 60, which
        // fell past the whole-break note and wrapped onto slice 12.
        static constexpr int wholeSampleNote = 60;                 // C3 = the WHOLE break
        static constexpr int baseNote = wholeSampleNote + 1;       // C#3 = slice 1, upward

        // The octave number that makes wholeSampleNote read as "C3". Any UI text naming
        // these notes derives from here, so a label can never drift from the map again.
        static constexpr int octaveNumForMiddleC = 3;
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

        // The most slices the UI offers (the 4/8/16/32/64 list tops out here).
        static constexpr int maxSlices = 64;

        void noteOn(int midiNoteNumber, float velocity) noexcept;

        // --- slice ORDER ---
        //
        // A pad plays order[pad], not slice `pad`. Without this indirection the note map is
        // the arrangement, so there is no way to rearrange a break at all — every beat
        // randomiser needs it first.
        //
        // Identity by default, so an untouched instrument behaves exactly as before.
        // Written on the message thread, read on the audio thread: entries are atomic ints,
        // and a read racing a write yields the old or the new slice for one note. That's a
        // different chop, once — not a crash, and far cheaper than locking the audio thread.
        void resetSliceOrder() noexcept;
        void setSliceOrder(const std::vector<int>& order) noexcept;
        std::vector<int> getSliceOrder() const;

        // Which slice pad `pad` plays. Clamped, and identity for out-of-range pads.
        int sliceForPad(int pad) const noexcept;

        juce::String sliceOrderToString() const;
        void sliceOrderFromString(const juce::String& s) noexcept;

        // --- per-SLICE settings ---
        //
        // Level, pan, transpose and reverse, per chop. The prototype's action bar: without
        // these a break can be rearranged but not shaped, so a too-loud snare or a hat that
        // wants pushing left has no answer short of editing the file.
        //
        // Indexed by SLICE, not by pad — so a setting stays with the sound it was dialled
        // in for when the break is shuffled, rather than staying with the position.
        //
        // Same threading as the order map: written on the message thread, read on the audio
        // thread, one atomic per field. A read racing a write yields the old or the new
        // value for one note.
        struct SliceSettingsView { float gain; float pan; float semitones; bool reverse; };

        void setSliceGain(int slice, float gain) noexcept;        // 0..1.5
        void setSlicePan(int slice, float pan) noexcept;          // -1..1
        void setSliceSemitones(int slice, float semis) noexcept;  // -24..24
        void setSliceReverse(int slice, bool rev) noexcept;
        SliceSettingsView getSliceSettings(int slice) const noexcept;
        void resetSliceSettings() noexcept;

        juce::String sliceSettingsToString() const;
        void sliceSettingsFromString(const juce::String& s) noexcept;

        // --- amplitude envelope ---
        // One curve applied to every slice as it fires, relative to the slice's own length.
        // Off by default and Flat is a no-op, so an untouched instrument is unchanged.
        void setAmpShape(const AmpShape& s, bool enabled) noexcept
        {
            ampShape = s;
            ampOn.store(enabled);
        }
        bool isAmpShapeOn() const noexcept { return ampOn.load(); }
        AmpShape getAmpShape() const noexcept { return ampShape; }

        // What each slice of the loaded break sounds like (kick / snare / hat / ...).
        // MESSAGE THREAD ONLY: it walks the whole sample and allocates. Empty if nothing
        // is loaded. The caller feeds this to musicalSliceOrder() to arrange the break
        // according to what the slices actually are.
        std::vector<SliceInfo> analyseCurrentSlices() const;

        // Gates the chop to the note, like a slicer should: hold a 16th, get a 16th-long
        // chop. Without this, every slice runs its full length and a fast pattern turns
        // into overlapping mush (and steals voices from itself).
        void noteOff(int midiNoteNumber) noexcept;

        void render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept;

        // HAUNT — the prototype's drone "conjured from your own slices". Picks the longest
        // slice (proxy for the most sustained/tonal material), loops it two octaves down
        // through a soft lowpass, swells the level in slowly, and ADDS it to the bus. The
        // caller routes this into the Space block so the drone gets the reverb/delay.
        // hauntAmt 0..100 %. Real-time safe. Off (and silent) at 0.
        void renderHaunt(juce::AudioBuffer<float>& bus, int startSample, int numSamples,
                         float hauntAmt) noexcept;

        // Transpose, in semitones. The grid's Pitch +/- lanes drive this: it applies to
        // voices started AFTER it's set, so a painted step transposes the chop that lands
        // on it and leaves whatever is already sounding alone.
        //
        // DIVERGENCE from the prototype, deliberate: the prototype changes playbackRate,
        // so a slice dropped an octave also takes twice as long and smears over the next
        // step. Here only the grain READ speed is transposed — the grain SPACING stays
        // native, so the chop keeps its length and still lands on the beat. On a grid lane
        // that is the point: a step effect that knocks the pattern out of time is a bug
        // wearing a feature's coat.
        void setPitchOffsetSemitones(float semitones) noexcept
        {
            pitchOffsetSemis = juce::jlimit(-24.0f, 24.0f, semitones);
        }
        float getPitchOffsetSemitones() const noexcept { return pitchOffsetSemis; }

        // Host tempo, so slices can be stretched to fit the grid. 0 = unknown (no stretch).
        void setHostBpm(double bpm) noexcept { hostBpm = bpm; }

        // Time-stretch on/off. Off = classic repitch (slice plays at its own speed).
        void setStretchEnabled(bool shouldStretch) noexcept { stretchEnabled = shouldStretch; }

        // How many superseded SampleData are still held. Exposed for tests: without it,
        // "the leak is fixed" is a claim rather than an observation. Should stay small
        // while audio is running, however many breaks you load.
        int getRetainedCount() const noexcept { return (int) retained.size(); }

        // Is any voice still sounding? The in-app audition uses this to loop the whole
        // break: re-trigger it the moment nothing is playing. Audio-thread safe (reads the
        // voice flags only).
        bool isSounding() const noexcept
        {
            for (const auto& v : voices) if (v.active) return true;
            return false;
        }

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
            // Per-slice shaping, captured at note-on so it stays put for this note.
            float panL = 1.0f, panR = 1.0f;
            bool reverse = false;
            float sliceSemis = 0.0f;

            int sliceIndex = -1;
            double outSample = 0.0;     // output samples since note-on
            double outDur = 0.0;        // total output samples for this slice
            double grainOut = 0.0;      // grain length, in OUTPUT samples
            double hopOut = 0.0;        // grain spacing out (= grainOut/2, 50% overlap)
            double hopIn = 0.0;         // grain spacing in INPUT samples
            double pitchRate = 1.0;     // input samples per output sample (transposed)
            double nativeRate = 1.0;    // ...and the untransposed rate, to anchor grain centres

            // Short fade on note-off so gating a chop doesn't click.
            double release = -1.0;      // samples remaining; < 0 = not releasing
            double releaseLen = 1.0;
        };

        juce::AudioFormatManager formats;

        // --- Haunt drone state (audio thread) ---
        const SampleData* hauntSeen = nullptr;   // to notice a sample change and re-pick
        double hauntPos = 0.0;                    // read position in source samples
        double hauntLoopStart = 0.0, hauntLoopEnd = 0.0;
        float  hauntGain = 0.0f;                  // smoothed toward the target level
        juce::dsp::IIR::Filter<float> hauntFiltL, hauntFiltR;
        int pickLongestSlice(const SampleData& s) const noexcept;

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

        // pad -> slice. See setSliceOrder for the threading argument.
        std::array<std::atomic<int>, (size_t) maxSlices> sliceOrder;

        // Per-slice settings, indexed by slice.
        std::array<std::atomic<float>, (size_t) maxSlices> sliceGain;
        std::array<std::atomic<float>, (size_t) maxSlices> slicePan;
        std::array<std::atomic<float>, (size_t) maxSlices> sliceSemis;
        std::array<std::atomic<bool>,  (size_t) maxSlices> sliceRev;

        // True when the order is anything other than 0,1,2,3...
        bool orderIsRearranged() const noexcept;

        // Whole-break playback SEQUENCES the slices when the break has been rearranged.
        //
        // Without this the whole-break note plays the raw file end to end, so the in-app
        // Play button ignored a shuffle completely — the numbers moved and the sound didn't.
        // Individual slice pads always honoured the order; it was only this path that
        // didn't, which is exactly the path the Play button uses.
        //
        // Only engaged when the order is REARRANGED. At identity the original single-voice
        // playback is used unchanged, so an untouched break is bit-for-bit as before rather
        // than newly stitched out of grains.
        int seqVoice = -1;      // which voice is playing the sequence, -1 = not sequencing
        int seqPad = 0;         // how far through the pads we are
        float seqGain = 1.0f;
        void startSeqSlice(const SampleData& s, int pad) noexcept;

        std::atomic<SampleData*> current { nullptr };
        std::array<Voice, maxVoices> voices;
        double hostRate = 44100.0;
        double hostBpm = 0.0;
        AmpShape ampShape = makeAmpShape(AmpShapeId::Flat);
        std::atomic<bool> ampOn { false };
        float pitchOffsetSemis = 0.0f;    // grid Pitch +/-; read at note-on
        bool stretchEnabled = true;
        SliceSettings slicing;
        juce::String sourcePath;
    };
}
