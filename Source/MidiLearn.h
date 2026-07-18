#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace Nebula2
{
    // MIDI CC -> parameter, so a hardware controller can drive the instrument.
    //
    // Threading is the whole design here. CCs arrive on the AUDIO thread, but moving a
    // parameter means setValueNotifyingHost, which notifies listeners, repaints, and in
    // some hosts allocates — none of which belongs in processBlock. So the audio thread
    // only RECORDS what arrived; the editor's timer applies it on the message thread.
    //
    // 128 CCs, one parameter each. A CC can drive one parameter and a parameter can be
    // driven by one CC: many-to-one would mean two knobs fighting over the same value, and
    // whichever moved last would win, which is indistinguishable from a bug.
    class MidiLearn
    {
    public:
        static constexpr int numCCs = 128;

        // --- learn ---
        // Arm with the parameter the user right-clicked. The next CC binds to it.
        void arm(const juce::String& paramId) { armed = paramId; }
        void disarm() { armed.clear(); }
        bool isArmed() const noexcept { return armed.isNotEmpty(); }
        juce::String armedParam() const { return armed; }

        // --- the map ---
        void bind(int cc, const juce::String& paramId);
        void clearCC(int cc);
        void clearParam(const juce::String& paramId);
        void clearAll();

        juce::String paramForCC(int cc) const;
        int ccForParam(const juce::String& paramId) const;   // -1 if unmapped

        // --- audio thread ---
        // Records a CC. Returns the id it just LEARNED (binding happens here so the very
        // CC that armed it also takes effect), or an empty string otherwise. Never
        // allocates on the mapped path: it writes into a fixed slot.
        juce::String noteCC(int cc, int value) noexcept;

        // --- message thread ---
        // Drains pending CC values into `out` (cc -> 0..1). Returns how many were pending.
        int drainPending(std::array<float, numCCs>& out, std::array<bool, numCCs>& has) noexcept;

        juce::String toString() const;
        void fromString(const juce::String&);

    private:
        std::array<juce::String, numCCs> map;         // message thread writes, audio reads by index
        std::array<std::atomic<float>, numCCs> pending {};
        std::array<std::atomic<bool>, numCCs> dirty {};
        juce::String armed;
    };
}
