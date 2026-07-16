#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>

namespace Nebula2
{
    enum class DrumVoiceId { Kick = 0, Snare, Clap, HatClosed, HatOpen, Tom, Rim, Perc, Count };

    // GM note map, taken from the prototype's own MIDI exporter so patterns it wrote play
    // back correctly here: 36 kick, 38 snare, 39 clap, 42 closed hat, 46 open hat, 45 tom,
    // 37 rim, 75 perc. Returns -1 for unmapped notes.
    int midiNoteToDrumVoice(int midiNoteNumber);

    // Plays the modal drum voices polyphonically.
    //
    // The synthesis is far too heavy (and allocates) to run on the audio thread, so every
    // voice is pre-rendered in prepare() across a few VELOCITY LAYERS — velocity changes
    // the timbre in this synthesis, not just the level, so one layer + gain would throw
    // away the character. noteOn/render are then allocation-free: just mixing buffers.
    class DrumKit
    {
    public:
        static constexpr int numVelocityLayers = 4;
        static constexpr int maxPolyphony = 32;

        void prepare(double sampleRate);   // pre-renders the kit (slow; never on the audio thread)
        void reset() noexcept;

        void noteOn(int midiNoteNumber, float velocity) noexcept;

        // Adds the active voices into `bus` (stereo, centred) for [startSample, +numSamples).
        void render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept;

        bool isPrepared() const noexcept { return prepared; }
        int activeVoiceCount() const noexcept;

    private:
        struct Active
        {
            const std::vector<float>* buf = nullptr;
            int pos = 0;
            float gain = 1.0f;
        };

        // [voice][velocity layer] -> pre-rendered one-shot
        std::array<std::array<std::vector<float>, numVelocityLayers>, (size_t) DrumVoiceId::Count> table;
        std::array<Active, maxPolyphony> voices;
        double sampleRate = 44100.0;
        bool prepared = false;
    };
}
