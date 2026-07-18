#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "GridView.h"
#include "MorphPadView.h"
#include "RackView.h"
#include "Nebula2LookAndFeel.h"
#include "Theme.h"
#include <array>

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
    // Page IDENTITY. Never reordered — the layout, paint and height switches all key off
    // these, and the running order is a separate list (tabOrder) so the tabs can be
    // rearranged without touching what any page means.
    enum class Page { play, morph, grid, rack, numPages };

    // Left-to-right running order of the tabs. GRID sits before MORPH: you build a pattern
    // and then shape it, so the grid is the earlier job.
    static const std::array<Page, 4>& tabOrder()
    {
        static const std::array<Page, 4> order { Page::play, Page::grid, Page::morph, Page::rack };
        return order;
    }

    static const char* tabName(Page p)
    {
        switch (p)
        {
            // "SAMPLE", not "PLAY": it selects the sample/colour/space PAGE, and a tab
            // labelled PLAY read like an audition button (that's the ▶ Play in the header).
            case Page::play:  return "SAMPLE";
            case Page::grid:  return "GRID";
            case Page::morph: return "MORPH";
            case Page::rack:  return "RACK";
            default:          return "?";
        }
    }
    Page page = Page::play;
    std::array<juce::TextButton, (size_t) Page::numPages> tabs;
    void showPage(Page);
    int contentHeightFor(Page) const;
    static int gridPageHeight();   // derived from the lane count, never hardcoded
    void rollGrid();               // the dice: eligible lanes only, at the chosen density

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

    Knob drive, crush, squeeze, tone, width, pump, master;
    Knob revMix, revSize, dlyMix, dlyFb, haunt;

    // The lane knobs (Resonate, Pitch +/-, Reverse, Stutter, Shatter, Gate). Held in a
    // vector and built by LOOPING Nebula2::extraColourControls(), so the list the
    // reachability gate checks and the widgets that actually exist are one thing. Named
    // members would let those drift, which is exactly how these seven lanes shipped
    // paintable and silent.
    std::vector<std::unique_ptr<Knob>> laneKnobs;
    juce::ComboBox resoKeyBox, resoScaleBox;
    juce::Label resoKeyLabel, resoScaleLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> resoKeyAttachment, resoScaleAttachment;

    juce::ComboBox charBox, revCharBox, dlySyncBox, dlyModeBox, sliceModeBox, sliceCountBox;
    juce::Label charLabel, revCharLabel, dlySyncLabel, dlyModeLabel, sliceModeLabel, sliceCountLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> dlyModeAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> charAttachment, revCharAttachment, dlySyncAttachment,
                                               sliceModeAttachment, sliceCountAttachment;
    Knob sensitivity;

    juce::TextButton colourRandButton { "Randomise" };
    juce::TextButton spaceRandButton { "Randomise" };
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
    juce::TextButton gridDiceButton { "Randomise" };
    juce::ComboBox gridDiceBox;          // Low / Mid / High — NOT a parameter (see processor)
    juce::Label gridDiceLabel;
    juce::ComboBox gridPatternBox;       // built-ins + your saved patterns
    juce::Label gridPatternLabel;
    juce::TextButton gridSaveButton { "Save" };
    juce::TextButton gridDeleteButton { "Delete" };
    void refreshGridPatternMenu();
    void saveGridPatternAs();

    // Morph pad
    MorphPadView morphPad { processorRef };
    juce::ToggleButton padOnButton { "Morph On" };
    std::unique_ptr<APVTS::ButtonAttachment> padOnAttachment;
    juce::ComboBox morphMotionBox, morphRateBox;
    juce::Label morphMotionLabel, morphRateLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> morphMotionAttachment, morphRateAttachment;
    Knob morphSize, morphGlide;
    // Four new scenes. Also the only way to change what the pad blends between: there is
    // no scene editor, so without this the corners are fixed at their seeds.
    juce::TextButton morphRandButton { "New Scenes" };

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
    // Rearranging the break. Shuffle permutes which slice each pad plays; Reset puts the
    // original order back, which matters because a shuffle is otherwise irreversible.
    juce::TextButton shuffleButton { "Shuffle" };
    juce::TextButton suggestBeatButton { "Suggest" };
    juce::TextButton resetOrderButton { "Reset Order" };
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
