#include "Theme.h"
#include "EqCurve.h"
#include "ParameterIDs.h"
#include "dsp/EqCurveMath.h"

namespace
{
    const auto& kWell   = Nebula2::Theme::well;
    const auto& kLine   = Nebula2::Theme::card3;
    const auto& kSub    = Nebula2::Theme::faint;
    const auto& kLive   = Nebula2::Theme::accent;
    const auto& kOffCol = Nebula2::Theme::dim;

    // Band shape must match RackModules::eqBandType. Ends are shelves, middle are peaks.
    int bandType (int i) noexcept
    {
        return i == 0 ? 1 : (i == Nebula2::ParamID::numEqBands - 1 ? 2 : 0);
    }
}

EqCurve::EqCurve (Nebula2AudioProcessor& p) : processorRef (p)
{
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

juce::RangedAudioParameter* EqCurve::param (const char* id) const
{
    return processorRef.getValueTreeState().getParameter (id);
}

float EqCurve::valueOf (const char* id, float fallback) const
{
    auto* raw = processorRef.getValueTreeState().getRawParameterValue (id);
    return raw != nullptr ? raw->load() : fallback;
}

bool EqCurve::bandOn (int band) const
{
    return valueOf (Nebula2::ParamID::eqOn[band], 1.0f) >= 0.5f;
}

juce::Point<float> EqCurve::nodePos (int band) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float x = r.getX() + r.getWidth()
                  * Nebula2::eqFreqToNorm (valueOf (Nebula2::ParamID::eqFreq[band], 1000.0f));
    const float y = r.getY() + r.getHeight()
                  * Nebula2::eqGainToNorm (valueOf (Nebula2::ParamID::eqGain[band], 0.0f));
    return { x, y };
}

int EqCurve::nodeAt (juce::Point<float> p) const
{
    // Nearest within a grab radius, rather than first-hit: overlapping nodes are normal on
    // an EQ, and first-hit would make the lower-numbered band impossible to escape.
    int best = -1;
    float bestD = nodeRadius * 3.0f;
    for (int i = 0; i < Nebula2::ParamID::numEqBands; ++i)
    {
        const float d = p.getDistanceFrom (nodePos (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

float EqCurve::curveDbAt (float hz) const
{
    float sum = 0.0f;
    for (int i = 0; i < Nebula2::ParamID::numEqBands; ++i)
    {
        if (! bandOn (i)) continue;      // an off band contributes nothing to the picture,
                                         // exactly as it contributes nothing to the sound
        sum += Nebula2::eqBandResponseDb (bandType (i),
                                          valueOf (Nebula2::ParamID::eqFreq[i], 1000.0f),
                                          valueOf (Nebula2::ParamID::eqQ[i], 1.0f),
                                          valueOf (Nebula2::ParamID::eqGain[i], 0.0f),
                                          hz);
    }
    return sum;
}

void EqCurve::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (kWell);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Decade gridlines, labelled. Without them the curve is a shape with no address and you
    // cannot tell 200 Hz from 2 kHz.
    g.setFont (juce::FontOptions (8.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = r.getX() + r.getWidth() * Nebula2::eqFreqToNorm (f);
        g.setColour (kLine);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (kSub.withAlpha (0.5f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k"
                                 : juce::String ((int) f),
                    (int) x + 2, (int) r.getBottom() - 10, 24, 9,
                    juce::Justification::left);
    }

    // 0 dB - the line the curve returns to, so a flat EQ is visibly flat.
    const float midY = r.getY() + r.getHeight() * 0.5f;
    g.setColour (kLine.brighter (0.15f));
    g.drawHorizontalLine ((int) midY, r.getX(), r.getRight());

    // The summed response.
    juce::Path curve;
    const int steps = juce::jmax (2, (int) r.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float db = curveDbAt (Nebula2::eqNormToFreq (t));
        const float x = r.getX() + r.getWidth() * t;
        const float y = r.getY() + r.getHeight() * Nebula2::eqGainToNorm (db);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (kLive.withAlpha (0.9f));
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // The nodes.
    for (int i = 0; i < Nebula2::ParamID::numEqBands; ++i)
    {
        const auto p = nodePos (i);
        const bool on = bandOn (i);
        const bool hot = (i == dragBand || i == hoverBand);

        // An OFF band is drawn as a ring, not hidden. Hiding it would leave a band you
        // cannot find to switch back on - the control would have removed itself.
        g.setColour (on ? kLive : kOffCol);
        if (on) g.fillEllipse (p.x - nodeRadius, p.y - nodeRadius, nodeRadius * 2, nodeRadius * 2);
        else    g.drawEllipse (p.x - nodeRadius, p.y - nodeRadius, nodeRadius * 2, nodeRadius * 2, 1.2f);

        if (hot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawEllipse (p.x - nodeRadius - 2.0f, p.y - nodeRadius - 2.0f,
                           (nodeRadius + 2.0f) * 2, (nodeRadius + 2.0f) * 2, 1.0f);
        }

        g.setColour (kSub);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String (i + 1), (int) p.x - 4, (int) p.y - 14, 8, 9,
                    juce::Justification::centred);
    }

    // The readout: frequency, gain and Q of whatever is being touched. This is the thing
    // that makes the band parametric rather than a shape you nudge and hope about.
    const int show = dragBand >= 0 ? dragBand : hoverBand;
    if (show >= 0)
    {
        const juce::String txt =
            Nebula2::eqFormatHz (valueOf (Nebula2::ParamID::eqFreq[show], 1000.0f))
            + "   " + juce::String (valueOf (Nebula2::ParamID::eqGain[show], 0.0f), 1) + " dB"
            + "   Q " + juce::String (valueOf (Nebula2::ParamID::eqQ[show], 1.0f), 2)
            + (bandOn (show) ? "" : "   OFF");

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (txt, r.reduced (4.0f, 2.0f), juce::Justification::topRight);
    }
}

void EqCurve::mouseDown (const juce::MouseEvent& e)
{
    dragBand = nodeAt (e.position);
    didDrag = false;
    if (dragBand < 0) return;

    // One gesture for the whole drag, per parameter, so the host records a single move.
    if (auto* f = param (Nebula2::ParamID::eqFreq[dragBand])) f->beginChangeGesture();
    if (auto* gp = param (Nebula2::ParamID::eqGain[dragBand])) gp->beginChangeGesture();
}

void EqCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    didDrag = true;

    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float tx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
    const float ty = juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));

    // setValueNotifyingHost takes NORMALISED values. The parameter's own range is skewed
    // (frequency is log), so converting through convertTo0to1 is what keeps the node under
    // the cursor instead of drifting away from it.
    if (auto* f = param (Nebula2::ParamID::eqFreq[dragBand]))
        f->setValueNotifyingHost (f->convertTo0to1 (Nebula2::eqNormToFreq (tx)));
    if (auto* gp = param (Nebula2::ParamID::eqGain[dragBand]))
        gp->setValueNotifyingHost (gp->convertTo0to1 (Nebula2::eqNormToGain (ty)));

    repaint();
}

void EqCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0) return;

    if (auto* f = param (Nebula2::ParamID::eqFreq[dragBand])) f->endChangeGesture();
    if (auto* gp = param (Nebula2::ParamID::eqGain[dragBand])) gp->endChangeGesture();

    // A click that never moved is a toggle. Done on mouse UP, and only when the pointer
    // stayed put, so switching a band off can't happen by accident mid-drag.
    if (! didDrag)
    {
        if (auto* on = param (Nebula2::ParamID::eqOn[dragBand]))
        {
            const bool now = bandOn (dragBand);
            on->beginChangeGesture();
            on->setValueNotifyingHost (now ? 0.0f : 1.0f);
            on->endChangeGesture();
        }
    }

    dragBand = -1;
    repaint();
}

void EqCurve::mouseMove (const juce::MouseEvent& e)
{
    const int was = hoverBand;
    hoverBand = nodeAt (e.position);
    if (was != hoverBand) repaint();
}

void EqCurve::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int band = dragBand >= 0 ? dragBand : nodeAt (e.position);
    if (band < 0) return;

    auto* q = param (Nebula2::ParamID::eqQ[band]);
    if (q == nullptr) return;

    // Step in NORMALISED space so the feel is even across the skewed Q range, rather than
    // crawling at one end and leaping at the other.
    const float t = juce::jlimit (0.0f, 1.0f, q->getValue() + w.deltaY * 0.6f);
    q->beginChangeGesture();
    q->setValueNotifyingHost (t);
    q->endChangeGesture();
    repaint();
}

void EqCurve::timerCallback()
{
    // Follow the PARAMETERS, so host automation and preset recalls move the nodes too. A
    // curve that only redrew on mouse input would sit still while the sound changed.
    float sig = 0.0f;
    for (int i = 0; i < Nebula2::ParamID::numEqBands; ++i)
        sig += valueOf (Nebula2::ParamID::eqFreq[i], 0.0f) * 0.001f
             + valueOf (Nebula2::ParamID::eqGain[i], 0.0f)
             + valueOf (Nebula2::ParamID::eqQ[i], 0.0f) * 7.0f
             + valueOf (Nebula2::ParamID::eqOn[i], 0.0f) * 131.0f;

    if (! juce::approximatelyEqual (sig, lastSeen[0]))
    {
        lastSeen[0] = sig;
        repaint();
    }
}
