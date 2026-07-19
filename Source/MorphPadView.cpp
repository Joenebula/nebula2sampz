#include "Theme.h"
#include "MorphPadView.h"
#include "ParameterIDs.h"

namespace
{
    const auto& kWell   = Nebula2::Theme::well;
    const auto& kAccent = Nebula2::Theme::accent;
    const auto& kSub    = Nebula2::Theme::sub;
    const auto& kLine   = Nebula2::Theme::line;
}

MorphPadView::MorphPadView(Nebula2AudioProcessor& p) : processorRef(p)
{
    startTimerHz(30);
}

bool MorphPadView::motionActive() const
{
    auto* m = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::morphMotion);
    return m != nullptr && m->load() > 0.5f;   // anything but "Off"
}

void MorphPadView::effectivePos(float& x, float& y) const
{
    if (motionActive())
    {
        // Auto-motion: follow the processor's glided position (updates while audio runs).
        x = processorRef.getMorphDrawX();
        y = processorRef.getMorphDrawY();
    }
    else
    {
        // Static: follow the params directly, so the dot tracks the mouse/automation with
        // no dependence on audio actually running.
        auto& v = processorRef.getValueTreeState();
        auto* px = v.getRawParameterValue(Nebula2::ParamID::padX);
        auto* py = v.getRawParameterValue(Nebula2::ParamID::padY);
        x = px != nullptr ? px->load() : 0.5f;
        y = py != nullptr ? py->load() : 0.5f;
    }
}

void MorphPadView::timerCallback()
{
    // Track the ON state too — the "pad is off" notice must appear/vanish the moment you
    // tick the box, not only when the dot happens to move.
    auto* on = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::padOn);
    const bool nowOn = on != nullptr && on->load() > 0.5f;

    float x, y; effectivePos(x, y);
    if (nowOn != lastOn || std::abs(x - lastX) > 0.0005f || std::abs(y - lastY) > 0.0005f)
    {
        lastOn = nowOn; lastX = x; lastY = y;
        repaint();
    }
}

juce::Point<float> MorphPadView::dotPos() const
{
    float x, y; effectivePos(x, y);
    return { x * (float) getWidth(), y * (float) getHeight() };
}

void MorphPadView::setFromMouse(const juce::MouseEvent& e)
{
    const float x = juce::jlimit(0.0f, 1.0f, (float) e.position.x / juce::jmax(1.0f, (float) getWidth()));
    const float y = juce::jlimit(0.0f, 1.0f, (float) e.position.y / juce::jmax(1.0f, (float) getHeight()));

    auto& v = processorRef.getValueTreeState();
    if (auto* px = v.getParameter(Nebula2::ParamID::padX)) px->setValueNotifyingHost(x);
    if (auto* py = v.getParameter(Nebula2::ParamID::padY)) py->setValueNotifyingHost(y);
    repaint();
}

void MorphPadView::mouseDown(const juce::MouseEvent& e) { setFromMouse(e); }
void MorphPadView::mouseDrag(const juce::MouseEvent& e) { setFromMouse(e); }

void MorphPadView::paint(juce::Graphics& g)
{
    g.fillAll(kWell);

    const auto b = getLocalBounds().toFloat();

    // Cross-hairs at the centre
    g.setColour(kLine);
    g.drawLine(b.getCentreX(), 0.0f, b.getCentreX(), b.getBottom());
    g.drawLine(0.0f, b.getCentreY(), b.getRight(), b.getCentreY());

    // Corner names, so the pad reads as a map of the sound
    g.setColour(kSub.withAlpha(0.75f));
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("open",   4, 3, 70, 12, juce::Justification::topLeft);
    g.drawText("dirty",  getWidth() - 74, 3, 70, 12, juce::Justification::topRight);
    g.drawText("dark",   4, getHeight() - 15, 70, 12, juce::Justification::bottomLeft);
    g.drawText("broken", getWidth() - 74, getHeight() - 15, 70, 12, juce::Justification::bottomRight);

    // The dot
    const auto p = dotPos();
    g.setColour(kAccent.withAlpha(0.18f));
    g.fillEllipse(p.x - 16.0f, p.y - 16.0f, 32.0f, 32.0f);
    g.setColour(kAccent);
    g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);

    // What you're hearing, right now — the blend, not the corners.
    //
    // Drawn in a strip along the BOTTOM, not across the middle. The middle is where the dot
    // lives (it starts dead centre), so the readout rendered as "drv 29[dot]g 19" — the one
    // number you most want to read, sitting under the thing you're dragging.
    const auto scene = Nebula2::blendMorph(processorRef.getMorphScenes(), lastX < 0 ? 0.5f : lastX,
                                           lastY < 0 ? 0.5f : lastY);
    juce::String txt;
    txt << (scene.cut >= 1000.0f ? juce::String(scene.cut / 1000.0f, 1) + "k" : juce::String((int) scene.cut))
        << "  res " << juce::String(scene.res, 1)
        << "  drv " << (int) scene.drv
        << "  flg " << (int) scene.flg
        << "  phs " << (int) scene.phs
        << "  sht " << (int) scene.sht;

    // SAY WHEN THE PAD IS OFF. A pad you can drag that changes nothing looks identical to a
    // working one — the prototype is blunt about this ("pad is off — the sliders own the
    // sound") and so is this. The user hit exactly this confusion.
    {
        auto* on = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::padOn);
        if (on == nullptr || on->load() <= 0.5f)
        {
            g.setColour(kWell.withAlpha(0.62f));
            g.fillAll();
            g.setColour(Nebula2::Theme::danger);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText("Pad is OFF - it won't touch the sound. Tick \"Morph On\".",
                       getLocalBounds(), juce::Justification::centred);
            return;
        }
    }

    auto strip = getLocalBounds().removeFromBottom(16).reduced(78, 0);   // clear of the corner labels
    g.setColour(kWell.withAlpha(0.85f));                                 // so a dot behind it can't
    g.fillRect(strip);                                                   // make it unreadable
    g.setColour(kSub);
    g.setFont(juce::FontOptions(9.0f));
    g.drawText(txt, strip, juce::Justification::centred);
}
