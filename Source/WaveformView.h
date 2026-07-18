#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/SampleLayer.h"

// The break, its slice markers, and the chop that's currently sounding.
//
// Follows the prototype's hardest-won rendering law: CACHE THE PICTURE, ANIMATE ONLY WHAT
// MOVES. The waveform is rendered once to an offscreen image and re-rendered only when the
// sample or the size actually changes — keyed deliberately NOT on the playhead. Per frame
// we just blit that image and draw the highlight + playhead on top. Rendering a full
// waveform every frame means scanning the whole buffer 60x a second, which is how a plugin
// ends up glitching audio while the UI is busy.
class WaveformView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit WaveformView(Nebula2::SampleLayer& layerToUse);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Call when a new sample is loaded — invalidates the cached picture.
    void sampleChanged();

private:
    void timerCallback() override;
    void rebuildCache();
    juce::String makeCacheKey() const;   // one definition: stamped AND compared

    Nebula2::SampleLayer& layer;
    juce::Image cache;
    juce::String cacheKey;          // what the picture depends on (NOT the playhead)
    uint32_t lastMask = 0;
    float lastPlayhead = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
