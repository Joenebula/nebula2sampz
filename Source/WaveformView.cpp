#include "WaveformView.h"

namespace
{
    const juce::Colour kWell   { 0xff05070d };
    const juce::Colour kWave   { 0xff5d6580 };
    const juce::Colour kAccent { 0xff3fe0d4 };
    const juce::Colour kLine   { 0x22ffffff };
    const juce::Colour kSub    { 0xff9aa3bd };
}

WaveformView::WaveformView(Nebula2::SampleLayer& layerToUse) : layer(layerToUse)
{
    startTimerHz(30);
}

void WaveformView::sampleChanged()
{
    cacheKey = {};      // force a rebuild on the next paint
    repaint();
}

void WaveformView::resized()
{
    cacheKey = {};      // the picture depends on size
}

void WaveformView::timerCallback()
{
    // Only repaint when something visible actually changed — not blindly at 30 Hz.
    const auto mask = layer.getPlayingSliceMask();
    const auto head = layer.getPlayheadNormalised();
    if (mask != lastMask || std::abs(head - lastPlayhead) > 0.0005f)
    {
        lastMask = mask;
        lastPlayhead = head;
        repaint();
    }
}

void WaveformView::rebuildCache()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    cache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(cache);

    g.fillAll(kWell);

    if (! layer.hasSample())
    {
        g.setColour(kSub);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText("Drop a break here, or Load Sample", getLocalBounds(), juce::Justification::centred, 1);
        return;
    }

    std::vector<float> mins, maxs;
    if (! layer.getWaveformPeaks(mins, maxs, w)) return;

    // Waveform
    const float mid = (float) h * 0.5f;
    g.setColour(kWave);
    for (int x = 0; x < w; ++x)
    {
        const float lo = juce::jlimit(-1.0f, 1.0f, mins[(size_t) x]);
        const float hi = juce::jlimit(-1.0f, 1.0f, maxs[(size_t) x]);
        const float y0 = mid - hi * mid * 0.92f;
        const float y1 = mid - lo * mid * 0.92f;
        g.drawVerticalLine(x, juce::jmin(y0, y1), juce::jmax(y0, y1) + 1.0f);
    }

    // Slice markers
    const auto bounds = layer.getSliceBoundariesNormalised();
    g.setColour(kLine);
    for (size_t i = 1; i + 1 < bounds.size(); ++i)
        g.drawVerticalLine((int) (bounds[i] * (float) w), 0.0f, (float) h);

    // Slice numbers, if there's room.
    //
    // These are the PAD that plays each slice, not the slice's own position. Labelling a
    // slice with its own index made Shuffle and Suggest look like buttons that did nothing:
    // the rearrangement was real and audible, but the picture was identical before and
    // after, so the only way to know was to play the thing. A number that changes when you
    // press the button is the difference between "it works" and "it looks broken".
    const int numSlices = (int) bounds.size() - 1;
    if (numSlices > 0 && w / numSlices >= 22)
    {
        // Invert the order: for slice s, which pad triggers it?
        std::vector<int> padForSlice((size_t) numSlices, -1);
        for (int pad = 0; pad < numSlices; ++pad)
        {
            const int s = layer.sliceForPad(pad);
            if (s >= 0 && s < numSlices && padForSlice[(size_t) s] < 0)
                padForSlice[(size_t) s] = pad;
        }

        g.setFont(juce::FontOptions(9.0f));
        for (int s = 0; s < numSlices; ++s)
        {
            const int x0 = (int) (bounds[(size_t) s] * (float) w);
            const int pad = padForSlice[(size_t) s];

            // A slice no pad reaches (possible after a re-slice to fewer slices) gets a
            // dash rather than a misleading number.
            if (pad < 0)
            {
                g.setColour(kSub.withAlpha(0.35f));
                g.drawText("-", x0 + 3, 2, 18, 10, juce::Justification::topLeft);
                continue;
            }

            // Lit when this slice has MOVED, so a rearranged break reads as rearranged at
            // a glance rather than needing to be compared number by number.
            const bool moved = (pad != s);
            g.setColour(moved ? kAccent.withAlpha(0.95f) : kSub.withAlpha(0.7f));
            g.drawText(juce::String(pad + 1), x0 + 3, 2, 18, 10, juce::Justification::topLeft);
        }
    }

    // Includes the slice ORDER (see makeCacheKey), or a shuffle would redraw the stale
    // picture from cache and the numbers would never move — leaving the button looking dead
    // for a second, different reason.
    cacheKey = makeCacheKey();
}

juce::String WaveformView::makeCacheKey() const
{
    // ONE definition, used both to stamp the cache and to decide whether it's stale. These
    // were two separate expressions that had to agree; adding the slice order to one and
    // not the other would leave the cache permanently fresh (never redrawn) or permanently
    // stale (redrawn every frame), and neither announces itself.
    return juce::String(getWidth()) + "x" + juce::String(getHeight()) + "|"
         + layer.getSampleName() + "|"
         + juce::String((int) layer.getSliceBoundariesNormalised().size())
         + "|" + layer.sliceOrderToString();
}

void WaveformView::paint(juce::Graphics& g)
{
    const juce::String wantKey = makeCacheKey();

    if (! cache.isValid() || cacheKey != wantKey)
        rebuildCache();

    g.drawImageAt(cache, 0, 0);

    if (! layer.hasSample()) return;

    const int w = getWidth();
    const int h = getHeight();
    const auto bounds = layer.getSliceBoundariesNormalised();
    const auto mask = layer.getPlayingSliceMask();

    // Light the slices that are sounding.
    if (mask != 0 && bounds.size() > 1)
    {
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
        {
            if ((mask & (1u << i)) == 0) continue;
            const float x0 = bounds[i] * (float) w;
            const float x1 = bounds[i + 1] * (float) w;
            g.setColour(kAccent.withAlpha(0.18f));
            g.fillRect(x0, 0.0f, x1 - x0, (float) h);
            g.setColour(kAccent.withAlpha(0.6f));
            g.drawVerticalLine((int) x0, 0.0f, (float) h);
        }
    }

    // Playhead
    const float head = layer.getPlayheadNormalised();
    if (head >= 0.0f)
    {
        g.setColour(kAccent);
        g.drawVerticalLine((int) (head * (float) w), 0.0f, (float) h);
    }
}
