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

    // What the PREVIEW button should say and whether it can be pressed.
    //
    // Pure, so the honesty rule is testable without a host. The rule: the button describes
    // what it will DO if clicked, and is disabled when clicking it can't do anything.
    //
    // It is called Preview, not Play, because it is not a transport — it auditions the break
    // inside the plugin and does not roll the DAW. Named "Play" it read as a transport
    // button, so it looked broken when starting the DAW didn't turn it into Stop.
    //
    // While the host rolls the preview loop is suppressed, so the label must NOT say "Stop"
    // even though the preview flag is still set: it would be offering to stop something that
    // isn't sounding.
    struct PreviewButtonState { juce::String text; bool enabled; };

    inline PreviewButtonState previewButtonState (bool previewFlagSet, bool hostRolling)
    {
        if (hostRolling)
            return { juce::String::fromUTF8("\xe2\x96\xb6 Preview"), false };   // DAW owns the sound

        return previewFlagSet ? PreviewButtonState{ juce::String::fromUTF8("\xe2\x96\xa0 Stop"), true }
                              : PreviewButtonState{ juce::String::fromUTF8("\xe2\x96\xb6 Preview"), true };
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
