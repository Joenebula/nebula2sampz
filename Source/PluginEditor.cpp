#include "PluginEditor.h"
#include "ParameterIDs.h"

namespace
{
    const juce::Colour kBg     { 0xff0b0e16 };
    const juce::Colour kPanel  { 0xff131a2e };
    const juce::Colour kAccent { 0xff3fe0d4 };
    const juce::Colour kSub    { 0xff9aa3bd };
}

Nebula2AudioProcessorEditor::Nebula2AudioProcessorEditor(Nebula2AudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Colour
    addKnob(drive,   Nebula2::ParamID::drive,   "Drive",   " %");
    addKnob(crush,   Nebula2::ParamID::crush,   "Crush",   " %");
    addKnob(squeeze, Nebula2::ParamID::squeeze, "Squeeze", " %");
    addKnob(tone,    Nebula2::ParamID::tone,    "Tone",    " %");
    addKnob(width,   Nebula2::ParamID::width,   "Width",   " %");
    addKnob(master,  Nebula2::ParamID::master,  "Master",  "");

    // Master reads as a gain 0..1 — show it as a percentage rather than "0.356".
    master.slider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    master.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    master.slider.updateText();

    // Space
    addKnob(revMix, Nebula2::ParamID::revMix, "Reverb", " %");
    addKnob(dlyMix, Nebula2::ParamID::dlyMix, "Delay",  " %");
    addKnob(dlyFb,  Nebula2::ParamID::dlyFb,  "Feedbk", " %");

    addCombo(charBox, charLabel, Nebula2::ParamID::driveChar, "Character",
             { "Tube", "Fuzz", "Fold" }, charAttachment);
    addCombo(revCharBox, revCharLabel, Nebula2::ParamID::revChar, "Reverb",
             { "Room", "Hall", "Plate", "Cathedral", "Reverse" }, revCharAttachment);
    addCombo(dlySyncBox, dlySyncLabel, Nebula2::ParamID::dlySync, "Sync",
             { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4." }, dlySyncAttachment);

    for (auto* b : { &fxOnButton, &limiterButton, &spaceOnButton })
    {
        b->setColour(juce::ToggleButton::textColourId, kSub);
        addAndMakeVisible(*b);
    }
    fxOnAttachment    = std::make_unique<APVTS::ButtonAttachment>(processorRef.getValueTreeState(), Nebula2::ParamID::fxOn, fxOnButton);
    limiterAttachment = std::make_unique<APVTS::ButtonAttachment>(processorRef.getValueTreeState(), Nebula2::ParamID::limiter, limiterButton);
    spaceOnAttachment = std::make_unique<APVTS::ButtonAttachment>(processorRef.getValueTreeState(), Nebula2::ParamID::spaceOn, spaceOnButton);

    // --- sample layer ---
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Load a break or loop",
                                                      juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc)
                             {
                                 const auto f = fc.getResult();
                                 if (f.existsAsFile()) loadSampleFile(f);
                             });
    };
    addAndMakeVisible(loadButton);

    sampleInfo.setColour(juce::Label::textColourId, kSub);
    sampleInfo.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sampleInfo);

    addCombo(sliceModeBox, sliceModeLabel, Nebula2::ParamID::sliceMode, "Slice",
             { "Grid", "Transient" }, sliceModeAttachment);
    addCombo(sliceCountBox, sliceCountLabel, Nebula2::ParamID::sliceCount, "Count",
             { "4", "8", "16", "32", "64" }, sliceCountAttachment);
    addKnob(sensitivity, Nebula2::ParamID::sensitivity, "Sens", "");
    sensitivity.slider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    sensitivity.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    sensitivity.slider.updateText();

    addAndMakeVisible(waveform);

    // --- FX grid ---
    addAndMakeVisible(gridView);
    gridOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    addAndMakeVisible(gridOnButton);
    gridOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::gridOn, gridOnButton);
    addCombo(gridStepsBox, gridStepsLabel, Nebula2::ParamID::gridSteps, "Steps",
             { "8", "16", "32" }, gridStepsAttachment);
    gridClearButton.onClick = [this] { processorRef.getGrid().clearAll(); gridView.repaint(); };
    addAndMakeVisible(gridClearButton);

    // --- Morph pad ---
    addAndMakeVisible(morphPad);
    padOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    addAndMakeVisible(padOnButton);
    padOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::padOn, padOnButton);

    // --- Modular rack ---
    addAndMakeVisible(rackView);
    rackOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    addAndMakeVisible(rackOnButton);
    rackOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::rackOn, rackOnButton);

    rackClearButton.onClick = [this] { rackView.clearPatch(); };
    addAndMakeVisible(rackClearButton);

    // Only the dials you'd reach for while patching are on the panel; the rest are still
    // full host parameters, so nothing here is a control the DAW can't see.
    addKnob(fltCut,   Nebula2::ParamID::fltCut,   "Cut",   " Hz");
    addKnob(fltRes,   Nebula2::ParamID::fltRes,   "Res",   "");
    addKnob(lfoRate,  Nebula2::ParamID::lfoRate,  "Rate",  " Hz");
    addKnob(lfoDepth, Nebula2::ParamID::lfoDepth, "Depth", " %");
    addKnob(cmbTune,  Nebula2::ParamID::cmbTune,  "Tune",  " Hz");
    addKnob(cmbFb,    Nebula2::ParamID::cmbFb,    "Comb FB", " %");
    addKnob(vowMorph, Nebula2::ParamID::vowMorph, "Vowel", "");
    addKnob(echTime,  Nebula2::ParamID::echTime,  "Echo",  " ms");
    addKnob(echFb,    Nebula2::ParamID::echFb,    "Echo FB", " %");
    addKnob(outLvl,   Nebula2::ParamID::outLvl,   "Rack Out", " %");

    addCombo(fltTypeBox, fltTypeLabel, Nebula2::ParamID::fltType, "Filter",
             { "Low Pass", "Band Pass", "High Pass" }, fltTypeAttachment);
    addCombo(lfoShapeBox, lfoShapeLabel, Nebula2::ParamID::lfoShape, "LFO",
             { "Sine", "Triangle", "Saw", "Square" }, lfoShapeAttachment);

    refreshSampleInfo();
    updateSliceControlStates();
    startTimerHz(8);        // watch for slice-mode changes (incl. from host automation)

    setSize(660, 1320);
}

void Nebula2AudioProcessorEditor::timerCallback()
{
    updateSliceControlStates();
}

void Nebula2AudioProcessorEditor::updateSliceControlStates()
{
    auto* p = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::sliceMode);
    const int mode = p != nullptr ? (int) p->load() : 0;
    if (mode == lastSliceModeSeen) return;
    lastSliceModeSeen = mode;

    const bool transient = (mode == 1);

    // In Transient mode the count comes from the detected onsets, so Count can't act.
    // In Grid mode there's no detection, so Sens can't act. Grey out whichever is inert
    // rather than letting it sit there looking operational.
    sliceCountBox.setEnabled(! transient);
    sliceCountLabel.setEnabled(! transient);
    sliceCountLabel.setColour(juce::Label::textColourId, transient ? kSub.withAlpha(0.35f) : kSub);

    sensitivity.slider.setEnabled(transient);
    sensitivity.label.setEnabled(transient);
    sensitivity.label.setColour(juce::Label::textColourId, transient ? kSub : kSub.withAlpha(0.35f));

    repaint();
}

void Nebula2AudioProcessorEditor::sampleReSliced()
{
    refreshSampleInfo();
    waveform.sampleChanged();
}

void Nebula2AudioProcessorEditor::loadSampleFile(const juce::File& f)
{
    // Message thread: decoding + slicing allocate, so they must never touch the audio thread.
    const bool ok = processorRef.getSampleLayer().loadFile(f);
    if (! ok)
    {
        sampleInfo.setText("Couldn't read that file", juce::dontSendNotification);
        return;
    }
    refreshSampleInfo();
    waveform.sampleChanged();   // the picture depends on the sample — invalidate the cache
}

void Nebula2AudioProcessorEditor::refreshSampleInfo()
{
    auto& layer = processorRef.getSampleLayer();
    if (! layer.hasSample())
    {
        sampleInfo.setText("No sample - drop a file here, or Load", juce::dontSendNotification);
        return;
    }

    const auto bpm = layer.getDetectedBpm();
    juce::String t = layer.getSampleName() + "  -  " + juce::String(layer.getNumSlices()) + " slices";
    if (bpm > 0.0) t += "  -  " + juce::String(bpm, 1) + " BPM";
    t += "   (B4 = whole break, C5+ = slices)";
    sampleInfo.setText(t, juce::dontSendNotification);
}

bool Nebula2AudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") || f.endsWithIgnoreCase(".aiff")
            || f.endsWithIgnoreCase(".flac") || f.endsWithIgnoreCase(".mp3"))
            return true;
    return false;
}

void Nebula2AudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    if (! files.isEmpty()) loadSampleFile(juce::File(files[0]));   // this refreshes the waveform
    repaint();
}

void Nebula2AudioProcessorEditor::fileDragEnter(const juce::StringArray&, int, int) { dragHighlight = true;  repaint(); }
void Nebula2AudioProcessorEditor::fileDragExit(const juce::StringArray&)            { dragHighlight = false; repaint(); }

void Nebula2AudioProcessorEditor::addKnob(Knob& k, const juce::String& paramID,
                                          const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    k.slider.setTextValueSuffix(suffix);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour(juce::Slider::thumbColourId, kAccent);
    k.slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, kSub);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<APVTS::SliderAttachment>(processorRef.getValueTreeState(), paramID, k.slider);
}

void Nebula2AudioProcessorEditor::addCombo(juce::ComboBox& box, juce::Label& label,
                                           const juce::String& paramID, const juce::String& name,
                                           const juce::StringArray& items,
                                           std::unique_ptr<APVTS::ComboBoxAttachment>& attachment)
{
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, kSub);
    addAndMakeVisible(label);

    box.addItemList(items, 1);
    addAndMakeVisible(box);
    attachment = std::make_unique<APVTS::ComboBoxAttachment>(processorRef.getValueTreeState(), paramID, box);
}

void Nebula2AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    auto header = getLocalBounds().removeFromTop(44).reduced(16, 8);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f));
    g.drawFittedText("Nebula2", header.removeFromLeft(100), juce::Justification::centredLeft, 1);
    g.setColour(kSub);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText("drums 36-46, 75    |    B4 whole break    |    C5+ slices",
                     header, juce::Justification::centredLeft, 1);

    auto body = getLocalBounds().reduced(12).withTrimmedTop(38);

    auto sampleArea = body.removeFromTop(188);
    g.setColour(dragHighlight ? kAccent.withAlpha(0.25f) : kPanel);
    g.fillRoundedRectangle(sampleArea.toFloat(), 8.0f);
    if (dragHighlight)
    {
        g.setColour(kAccent);
        g.drawRoundedRectangle(sampleArea.toFloat().reduced(1.0f), 8.0f, 1.5f);
    }
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(10.0f));
    g.drawFittedText("SAMPLE", sampleArea.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);

    body.removeFromTop(8);
    auto colour = body.removeFromTop(200);
    g.setColour(kPanel);
    g.fillRoundedRectangle(colour.toFloat(), 8.0f);

    body.removeFromTop(8);
    auto space = body.removeFromTop(120);
    g.fillRoundedRectangle(space.toFloat(), 8.0f);

    body.removeFromTop(8);
    auto morphArea = body.removeFromTop(170);
    g.fillRoundedRectangle(morphArea.toFloat(), 8.0f);

    body.removeFromTop(8);
    auto gridArea = body.removeFromTop(150);
    g.fillRoundedRectangle(gridArea.toFloat(), 8.0f);

    body.removeFromTop(8);
    g.fillRoundedRectangle(body.toFloat(), 8.0f);

    g.setColour(kAccent);
    g.setFont(juce::FontOptions(10.0f));
    g.drawFittedText("COLOUR", colour.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);
    g.drawFittedText("SPACE", space.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);
    g.drawFittedText("MORPH", morphArea.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);
    g.drawFittedText("GRID", gridArea.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);
    g.drawFittedText("RACK", body.reduced(12, 6).removeFromTop(12), juce::Justification::topLeft, 1);
}

void Nebula2AudioProcessorEditor::resized()
{
    auto body = getLocalBounds().reduced(12).withTrimmedTop(38);

    // --- Sample panel ---
    auto sampleArea = body.removeFromTop(188).reduced(10);
    sampleArea.removeFromTop(12);
    auto sRow2 = sampleArea.removeFromTop(26);
    loadButton.setBounds(sRow2.removeFromLeft(120));
    sRow2.removeFromLeft(12);
    sampleInfo.setBounds(sRow2);

    sampleArea.removeFromTop(4);
    auto sliceRow = sampleArea.removeFromTop(34);
    sliceModeLabel.setBounds(sliceRow.removeFromLeft(38));
    sliceModeBox.setBounds(sliceRow.removeFromLeft(92).reduced(0, 5));
    sliceRow.removeFromLeft(10);
    sliceCountLabel.setBounds(sliceRow.removeFromLeft(42));
    sliceCountBox.setBounds(sliceRow.removeFromLeft(64).reduced(0, 5));
    sliceRow.removeFromLeft(14);
    sensitivity.label.setBounds(sliceRow.removeFromLeft(38));
    sensitivity.slider.setBounds(sliceRow.removeFromLeft(70));

    sampleArea.removeFromTop(4);
    waveform.setBounds(sampleArea);
    body.removeFromTop(8);

    // --- Colour panel ---
    auto colour = body.removeFromTop(200).reduced(10);
    colour.removeFromTop(12);
    auto knobRow = colour.removeFromTop(120);
    Knob* cKnobs[] = { &drive, &crush, &squeeze, &tone, &width, &master };
    const int cw = knobRow.getWidth() / 6;
    for (auto* k : cKnobs)
    {
        auto cell = knobRow.removeFromLeft(cw);
        k->label.setBounds(cell.removeFromTop(16));
        k->slider.setBounds(cell.reduced(3));
    }
    colour.removeFromTop(8);
    auto cRow = colour.removeFromTop(24);
    charLabel.setBounds(cRow.removeFromLeft(64));
    charBox.setBounds(cRow.removeFromLeft(100).reduced(0, 1));
    cRow.removeFromLeft(16);
    fxOnButton.setBounds(cRow.removeFromLeft(80));
    limiterButton.setBounds(cRow.removeFromLeft(80));

    // --- Space panel ---
    body.removeFromTop(8);
    auto sp = body.removeFromTop(120).reduced(10);
    sp.removeFromTop(12);
    auto sRow = sp.removeFromTop(90);
    Knob* sKnobs[] = { &revMix, &dlyMix, &dlyFb };
    const int sw = 90;
    for (auto* k : sKnobs)
    {
        auto cell = sRow.removeFromLeft(sw);
        k->label.setBounds(cell.removeFromTop(16));
        k->slider.setBounds(cell.reduced(3));
    }
    sRow.removeFromLeft(12);
    auto right = sRow.removeFromTop(24);
    revCharLabel.setBounds(right.removeFromLeft(50));
    revCharBox.setBounds(right.removeFromLeft(96).reduced(0, 1));
    right.removeFromLeft(10);
    dlySyncLabel.setBounds(right.removeFromLeft(36));
    dlySyncBox.setBounds(right.removeFromLeft(70).reduced(0, 1));
    right.removeFromLeft(10);
    spaceOnButton.setBounds(right.removeFromLeft(86));

    // --- Morph panel ---
    body.removeFromTop(8);
    auto mp = body.removeFromTop(170).reduced(10);
    mp.removeFromTop(12);
    auto mRow = mp.removeFromTop(24);
    padOnButton.setBounds(mRow.removeFromLeft(90));
    mp.removeFromTop(4);
    morphPad.setBounds(mp);

    // --- Grid panel ---
    body.removeFromTop(8);
    auto gp = body.removeFromTop(150).reduced(10);
    gp.removeFromTop(12);
    auto gRow = gp.removeFromTop(26);
    gridOnButton.setBounds(gRow.removeFromLeft(78));
    gRow.removeFromLeft(8);
    gridStepsLabel.setBounds(gRow.removeFromLeft(40));
    gridStepsBox.setBounds(gRow.removeFromLeft(64).reduced(0, 2));
    gRow.removeFromLeft(12);
    gridClearButton.setBounds(gRow.removeFromLeft(60).reduced(0, 2));
    gp.removeFromTop(6);
    gridView.setBounds(gp);

    // --- Rack panel ---
    body.removeFromTop(8);
    auto rp = body.reduced(10);
    rp.removeFromTop(12);
    auto rRow = rp.removeFromTop(26);
    rackOnButton.setBounds(rRow.removeFromLeft(78));
    rRow.removeFromLeft(8);
    rackClearButton.setBounds(rRow.removeFromLeft(84).reduced(0, 2));
    rRow.removeFromLeft(12);
    fltTypeLabel.setBounds(rRow.removeFromLeft(38));
    fltTypeBox.setBounds(rRow.removeFromLeft(84).reduced(0, 2));
    rRow.removeFromLeft(8);
    lfoShapeLabel.setBounds(rRow.removeFromLeft(28));
    lfoShapeBox.setBounds(rRow.removeFromLeft(80).reduced(0, 2));

    rp.removeFromTop(4);
    rackView.setBounds(rp.removeFromTop(rp.getHeight() - 78));

    rp.removeFromTop(4);
    auto kRow = rp.removeFromTop(72);
    const int kw = kRow.getWidth() / 10;
    Knob* rackKnobs[] = { &fltCut, &fltRes, &lfoRate, &lfoDepth, &cmbTune,
                          &cmbFb, &vowMorph, &echTime, &echFb, &outLvl };
    for (auto* k : rackKnobs)
    {
        auto cell = kRow.removeFromLeft(kw);
        k->label.setBounds(cell.removeFromTop(12));
        k->slider.setBounds(cell);
    }
}
