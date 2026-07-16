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

    // Slice numbers, if there's room
    if (bounds.size() > 1 && w / (int) (bounds.size() - 1) >= 22)
    {
        g.setColour(kSub.withAlpha(0.7f));
        g.setFont(juce::FontOptions(9.0f));
        for (size_t i = 0; i + 1 < bounds.size(); ++i)
        {
            const int x0 = (int) (bounds[i] * (float) w);
            g.drawText(juce::String((int) i + 1), x0 + 3, 2, 18, 10, juce::Justification::topLeft);
        }
    }

    cacheKey = juce::String(w) + "x" + juce::String(h) + "|"
             + layer.getSampleName() + "|" + juce::String((int) bounds.size());
}

void WaveformView::paint(juce::Graphics& g)
{
    const juce::String wantKey = juce::String(getWidth()) + "x" + juce::String(getHeight()) + "|"
                              + layer.getSampleName() + "|"
                              + juce::String((int) layer.getSliceBoundariesNormalised().size());

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
