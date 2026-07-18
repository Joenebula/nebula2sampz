#include "MidiLearn.h"

namespace Nebula2
{
    void MidiLearn::bind(int cc, const juce::String& paramId)
    {
        if (cc < 0 || cc >= numCCs || paramId.isEmpty()) return;

        // One CC per parameter: rebinding moves it rather than adding a second source.
        // Two CCs on one knob means whichever moved last wins, which reads as a bug.
        clearParam(paramId);
        map[(size_t) cc] = paramId;
    }

    void MidiLearn::clearCC(int cc)
    {
        if (cc < 0 || cc >= numCCs) return;
        map[(size_t) cc].clear();
    }

    void MidiLearn::clearParam(const juce::String& paramId)
    {
        for (auto& m : map) if (m == paramId) m.clear();
    }

    void MidiLearn::clearAll()
    {
        for (auto& m : map) m.clear();
    }

    juce::String MidiLearn::paramForCC(int cc) const
    {
        if (cc < 0 || cc >= numCCs) return {};
        return map[(size_t) cc];
    }

    int MidiLearn::ccForParam(const juce::String& paramId) const
    {
        if (paramId.isEmpty()) return -1;
        for (int i = 0; i < numCCs; ++i) if (map[(size_t) i] == paramId) return i;
        return -1;
    }

    juce::String MidiLearn::noteCC(int cc, int value) noexcept
    {
        if (cc < 0 || cc >= numCCs) return {};

        // Learning happens HERE, so the CC that binds also moves the control. Binding on
        // the next one instead means the first turn of the knob does nothing, which feels
        // broken even though it isn't.
        juce::String learned;
        if (armed.isNotEmpty())
        {
            learned = armed;
            bind(cc, armed);
            armed.clear();
        }

        pending[(size_t) cc].store(juce::jlimit(0.0f, 1.0f, (float) value / 127.0f));
        dirty[(size_t) cc].store(true);
        return learned;
    }

    int MidiLearn::drainPending(std::array<float, numCCs>& out, std::array<bool, numCCs>& has) noexcept
    {
        int n = 0;
        for (int i = 0; i < numCCs; ++i)
        {
            has[(size_t) i] = dirty[(size_t) i].exchange(false);
            if (has[(size_t) i]) { out[(size_t) i] = pending[(size_t) i].load(); ++n; }
        }
        return n;
    }

    juce::String MidiLearn::toString() const
    {
        // Only the bound ones — a 128-entry list of mostly nothing is noise in the project
        // file and slower to parse than it needs to be.
        juce::String s;
        for (int i = 0; i < numCCs; ++i)
            if (map[(size_t) i].isNotEmpty())
            {
                if (s.isNotEmpty()) s << ",";
                s << i << "=" << map[(size_t) i];
            }
        return s;
    }

    void MidiLearn::fromString(const juce::String& str)
    {
        clearAll();
        if (str.isEmpty()) return;

        for (const auto& pair : juce::StringArray::fromTokens(str, ",", ""))
        {
            const int eq = pair.indexOfChar('=');
            if (eq <= 0) continue;                       // skip the entry, keep the rest
            const auto ccStr = pair.substring(0, eq).trim();
            if (! ccStr.containsOnly("0123456789")) continue;
            bind(ccStr.getIntValue(), pair.substring(eq + 1).trim());
        }
    }
}
