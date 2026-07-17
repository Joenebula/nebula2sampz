#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "GridView.h"
#include "MorphPadView.h"
#include "RackView.h"
#include "Nebula2LookAndFeel.h"
#include "Theme.h"

// A functional control surface: every live parameter, attached to the APVTS so the host
// and the UI can never disagree (law: one source of truth — the visual derives from state).
// This is deliberately plain — the designed UI is Phase 5. It exists so the FX can be
// judged BY EAR, which is the only test that matters for whether the port sounds right.
class Nebula2AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::FileDragAndDropTarget,
                                          private juce::Timer
{
public:
    explicit Nebula2AudioProcessorEditor(Nebula2AudioProcessor&);
    ~Nebula2AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Called by the processor after an off-thread re-slice, so the waveform redraws.
    void sampleReSliced();

    // Drop a break straight onto the plugin.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    // The panels now add up to more height than a laptop screen has, so the surface
    // SCROLLS rather than being clipped by the host window. This was a real defect, not a
    // nicety: the rack panel existed, was wired, and was simply below the bottom edge —
    // which is indistinguishable from "the rack isn't there".
    //
    // A viewport needs a component to scroll. This forwards paint/resized back to the
    // editor so the panel-drawing code stays in one place rather than being duplicated.
    struct ScrollingContent final : public juce::Component
    {
        std::function<void(juce::Graphics&)> onPaint;
        std::function<void()> onResized;
        void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
        void resized() override { if (onResized) onResized(); }
    };

    Nebula2LookAndFeel lnf;

    // --- UI scale ---
    // The prototype's type sizes (9.5-11px) are CSS pixels, which a browser MULTIPLIES by
    // the OS display scaling before drawing. A JUCE editor gets no such multiplier — it
    // draws at raw pixels. So porting the numbers faithfully produced a UI that was
    // 1.25-1.5x smaller than the thing being copied, and genuinely unreadable.
    //
    // The fix is to restore the multiplier, not to inflate every font: the layout stays in
    // the prototype's own units and the whole surface scales, exactly as the browser did.
    // Defaults to the OS display scale for that reason. Not a parameter — it's a property
    // of this screen, not of the song, so it must never be automated or travel in a preset.
    float uiScale = 1.0f;
    juce::ComboBox zoomBox;
    void applyScale();
    static float defaultUiScale();

    juce::Viewport viewport;
    ScrollingContent content;

    // Pages. The instrument has six blocks and they no longer fit on one surface — the
    // scroll was a stop-gap for exactly that. Tabs beat scrolling here because the blocks
    // are separate jobs (choose a break / colour it / patch a rack), not one long list you
    // read top to bottom.
    enum class Page { play, morph, grid, rack, numPages };
    Page page = Page::play;
    std::array<juce::TextButton, (size_t) Page::numPages> tabs;
    void showPage(Page);
    int contentHeightFor(Page) const;

    // In-app audition: play the loaded break without rolling the DAW. Reflects the
    // processor's state (which the host transport can clear), so the Timer keeps the
    // button's label honest rather than trusting the last click.
    juce::TextButton auditionButton { "▶ Play" };

    void paintContent(juce::Graphics&);
    void layoutContent();

    // A page can still be taller than a small window, so the viewport stays — but now it
    // scrolls one page's worth, not the whole instrument.
    static constexpr int headerH = 74;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    void addKnob(Knob& k, const juce::String& paramID, const juce::String& name, const juce::String& suffix);
    void addCombo(juce::ComboBox& box, juce::Label& label, const juce::String& paramID,
                  const juce::String& name, const juce::StringArray& items,
                  std::unique_ptr<APVTS::ComboBoxAttachment>& attachment);

    Nebula2AudioProcessor& processorRef;

    Knob drive, crush, squeeze, tone, width, master;
    Knob revMix, revSize, dlyMix, dlyFb;

    juce::ComboBox charBox, revCharBox, dlySyncBox, sliceModeBox, sliceCountBox;
    juce::Label charLabel, revCharLabel, dlySyncLabel, sliceModeLabel, sliceCountLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> charAttachment, revCharAttachment, dlySyncAttachment,
                                               sliceModeAttachment, sliceCountAttachment;
    Knob sensitivity;

    juce::ToggleButton spaceOnButton { "Space On" };
    std::unique_ptr<APVTS::ButtonAttachment> spaceOnAttachment;

    // FX grid
    GridView gridView { processorRef };
    juce::ToggleButton gridOnButton { "Grid On" };
    std::unique_ptr<APVTS::ButtonAttachment> gridOnAttachment;
    juce::ComboBox gridStepsBox;
    juce::Label gridStepsLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> gridStepsAttachment;
    juce::TextButton gridClearButton { "Clear" };

    // Morph pad
    MorphPadView morphPad { processorRef };
    juce::ToggleButton padOnButton { "Morph On" };
    std::unique_ptr<APVTS::ButtonAttachment> padOnAttachment;

    // Modular rack
    RackView rackView { processorRef };
    juce::ToggleButton rackOnButton { "Rack On" };
    std::unique_ptr<APVTS::ButtonAttachment> rackOnAttachment;
    juce::TextButton rackClearButton { "Clear Patch" };
    // Filter type and LFO shape stay on the panel: they're choices, not dials, and a combo
    // box inside a 100px module box would be unusable. Everything else is on its module.
    juce::ComboBox fltTypeBox, lfoShapeBox;
    juce::Label fltTypeLabel, lfoShapeLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> fltTypeAttachment, lfoShapeAttachment;

    // Sample layer
    WaveformView waveform { processorRef.getSampleLayer() };
    juce::TextButton loadButton { "Load Sample..." };
    juce::Label sampleInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHighlight = false;

    void loadSampleFile(const juce::File& f);
    void refreshSampleInfo();

    // Law 4 (from the prototype's own hard-won list): a control that cannot act must SAY
    // so. Count does nothing in Transient mode; Sens does nothing in Grid mode. Leaving
    // them live-looking but inert is exactly the silent failure that wastes an hour.
    void timerCallback() override;
    void updateSliceControlStates();
    int lastSliceModeSeen = -1;

    juce::ToggleButton fxOnButton { "FX On" };
    std::unique_ptr<APVTS::ButtonAttachment> fxOnAttachment;

    juce::ToggleButton limiterButton { "Limiter" };
    std::unique_ptr<APVTS::ButtonAttachment> limiterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessorEditor)
};
