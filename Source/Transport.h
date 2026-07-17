#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace Nebula2
{
    // Is the host genuinely rolling? Judged by its timeline POSITION advancing, NOT by
    // getIsPlaying() — some hosts report isPlaying=true while stopped, and gating the in-app
    // audition on that flag made it clear itself every block and never sound. A static ppq
    // means stopped; an advancing ppq means playing. lastPpq < 0 means "no prior block yet".
    inline bool hostIsRolling(double ppq, double lastPpq) noexcept
    {
        return lastPpq >= 0.0 && std::abs(ppq - lastPpq) > 1.0e-9;
    }

    // Host transport snapshot the engine cares about. Musical time, not milliseconds.
    struct TransportState
    {
        double bpm = 120.0;
        double ppq = 0.0;   // quarter-note position in the timeline
        bool playing = false;
    };

    // Pure read of a host PositionInfo -> TransportState. Header-only + side-effect-free
    // so it can be unit-tested with a hand-built PositionInfo (no live host needed).
    inline TransportState readTransport(const juce::AudioPlayHead::PositionInfo& pos) noexcept
    {
        TransportState t;
        if (auto b = pos.getBpm())          t.bpm = *b;
        if (auto p = pos.getPpqPosition())  t.ppq = *p;
        t.playing = pos.getIsPlaying();
        return t;
    }
}
