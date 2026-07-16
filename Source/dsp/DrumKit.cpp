#include "DrumKit.h"
#include "DrumSynth.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    int midiNoteToDrumVoice(int note)
    {
        switch (note)
        {
            case 36: return (int) DrumVoiceId::Kick;
            case 38: return (int) DrumVoiceId::Snare;
            case 39: return (int) DrumVoiceId::Clap;
            case 42: return (int) DrumVoiceId::HatClosed;
            case 46: return (int) DrumVoiceId::HatOpen;
            case 45: return (int) DrumVoiceId::Tom;
            case 37: return (int) DrumVoiceId::Rim;
            case 75: return (int) DrumVoiceId::Perc;
            default: return -1;
        }
    }

    namespace
    {
        // Stable per-voice seed, in the spirit of the prototype's dmLib (id -> seed).
        uint32_t seedFor(int voice) { return (uint32_t) (voice * 131 + 7); }

        std::vector<float> renderVoice(int voice, float vel, double sr)
        {
            switch ((DrumVoiceId) voice)
            {
                case DrumVoiceId::Kick:      return Drums::vKick (vel, seedFor(voice), sr);
                case DrumVoiceId::Snare:     return Drums::vSnare(vel, seedFor(voice), sr);
                case DrumVoiceId::Clap:      return Drums::vClap (vel, seedFor(voice), sr);
                case DrumVoiceId::HatClosed: return Drums::vHat  (vel, seedFor(voice), sr, false);
                case DrumVoiceId::HatOpen:   return Drums::vHat  (vel, seedFor(voice), sr, true);
                case DrumVoiceId::Tom:       return Drums::vTom  (vel, seedFor(voice), sr);
                case DrumVoiceId::Rim:       return Drums::vRim  (vel, seedFor(voice), sr);
                case DrumVoiceId::Perc:      return Drums::vPerc (vel, seedFor(voice), sr);
                default:                     return {};
            }
        }
    }

    void DrumKit::prepare(double sr)
    {
        sampleRate = sr;
        for (int v = 0; v < (int) DrumVoiceId::Count; ++v)
            for (int l = 0; l < numVelocityLayers; ++l)
            {
                const float layerVel = (float) (l + 1) / (float) numVelocityLayers;   // .25 .5 .75 1
                table[(size_t) v][(size_t) l] = renderVoice(v, layerVel, sr);
            }
        reset();
        prepared = true;
    }

    void DrumKit::reset() noexcept
    {
        for (auto& a : voices) { a.buf = nullptr; a.pos = 0; a.gain = 1.0f; }
    }

    int DrumKit::activeVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& a : voices) if (a.buf != nullptr) ++n;
        return n;
    }

    void DrumKit::noteOn(int note, float velocity) noexcept
    {
        const int v = midiNoteToDrumVoice(note);
        if (v < 0 || ! prepared) return;

        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        // Pick the nearest velocity layer for timbre, then trim gain to smooth the steps.
        int layer = (int) std::lround(vel * (float) numVelocityLayers) - 1;
        layer = juce::jlimit(0, numVelocityLayers - 1, layer);
        const float layerVel = (float) (layer + 1) / (float) numVelocityLayers;

        const auto& buf = table[(size_t) v][(size_t) layer];
        if (buf.empty()) return;

        // Free slot, else steal the most-finished voice.
        int slot = -1;
        for (int i = 0; i < maxPolyphony; ++i) if (voices[(size_t) i].buf == nullptr) { slot = i; break; }
        if (slot < 0)
        {
            int bestProgress = -1;
            for (int i = 0; i < maxPolyphony; ++i)
                if (voices[(size_t) i].pos > bestProgress) { bestProgress = voices[(size_t) i].pos; slot = i; }
        }

        voices[(size_t) slot].buf = &buf;
        voices[(size_t) slot].pos = 0;
        voices[(size_t) slot].gain = juce::jlimit(0.5f, 1.5f, layerVel > 0.0f ? vel / layerVel : 1.0f);
    }

    void DrumKit::render(juce::AudioBuffer<float>& bus, int startSample, int numSamples) noexcept
    {
        if (numSamples <= 0) return;
        const int numChannels = bus.getNumChannels();

        for (auto& a : voices)
        {
            if (a.buf == nullptr) continue;
            const auto& src = *a.buf;
            const int remaining = (int) src.size() - a.pos;
            const int count = std::min(numSamples, remaining);

            for (int i = 0; i < count; ++i)
            {
                const float s = src[(size_t) (a.pos + i)] * a.gain;
                for (int c = 0; c < numChannels; ++c)
                    bus.addSample(c, startSample + i, s);          // mono voice, centred
            }

            a.pos += count;
            if (a.pos >= (int) src.size()) { a.buf = nullptr; a.pos = 0; }   // finished
        }
    }
}
