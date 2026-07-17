#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "GridView.h"
#include "MorphPadView.h"
#include "RackView.h"

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
    ~Nebula2AudioProcessorEditor() override = default;

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
    Knob revMix, dlyMix, dlyFb;

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
    Knob fltCut, fltRes, cmbTune, cmbFb, vowMorph, echTime, echFb, outLvl;
    juce::ComboBox fltTypeBox, lfoShapeBox;
    juce::Label fltTypeLabel, lfoShapeLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> fltTypeAttachment, lfoShapeAttachment;
    Knob lfoRate, lfoDepth;

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
