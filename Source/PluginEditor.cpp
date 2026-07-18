#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "GridPresets.h"
#include "dsp/SliceAnalysis.h"
#include "Randomise.h"

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
    // One look-and-feel for the whole editor, set before any child is made, so nothing
    // gets a chance to render in JUCE's default grey first.
    setLookAndFeel(&lnf);

    // Colour
    addKnob(pump,    Nebula2::ParamID::pump,    "Pump",    " %");
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

    // Space — Reverb and Delay are now clearly separate sections (the user's request:
    // "separate controls"). Under REVERB/DELAY headers the knobs can just say Mix.
    addKnob(revMix,  Nebula2::ParamID::revMix,  "Mix",  " %");
    addKnob(revSize, Nebula2::ParamID::revSize, "Size", " %");
    addKnob(dlyMix,  Nebula2::ParamID::dlyMix,  "Mix",  " %");
    addKnob(dlyFb,   Nebula2::ParamID::dlyFb,   "Feedback", " %");
    addKnob(haunt,   Nebula2::ParamID::haunt,   "Haunt", " %");

    // The lane knobs, built by LOOPING the shared table — see PluginEditor.h. These seven
    // lanes had working DSP and no way to reach it: the grid blends toward a panel amount
    // that sat at 0 and could only be moved by DAW automation.
    for (const auto& spec : Nebula2::extraColourControls())
    {
        auto k = std::make_unique<Knob>();
        addKnob(*k, spec.paramId, spec.label, spec.suffix);
        laneKnobs.push_back(std::move(k));
    }

    addCombo(resoKeyBox, resoKeyLabel, Nebula2::ParamID::resoKey, "Key",
             { "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#" },
             resoKeyAttachment);
    addCombo(resoScaleBox, resoScaleLabel, Nebula2::ParamID::resoScale, "Scale",
             { "Minor", "Major", "Phrygian", "Fifths" }, resoScaleAttachment);

    colourRandButton.onClick = [this]
    {
        juce::Random rng;
        Nebula2::applyRolls(processorRef.getValueTreeState(), Nebula2::randomColourValues(rng));
    };
    content.addAndMakeVisible(colourRandButton);

    spaceRandButton.onClick = [this]
    {
        juce::Random rng;
        Nebula2::applyRolls(processorRef.getValueTreeState(), Nebula2::randomSpaceValues(rng));
    };
    content.addAndMakeVisible(spaceRandButton);

    addCombo(charBox, charLabel, Nebula2::ParamID::driveChar, "Character",
             { "Tube", "Fuzz", "Fold" }, charAttachment);
    addCombo(revCharBox, revCharLabel, Nebula2::ParamID::revChar, "Reverb",
             { "Room", "Hall", "Plate", "Cathedral", "Reverse" }, revCharAttachment);
    addCombo(dlySyncBox, dlySyncLabel, Nebula2::ParamID::dlySync, "Sync",
             { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4." }, dlySyncAttachment);
    addCombo(dlyModeBox, dlyModeLabel, Nebula2::ParamID::dlyMode, "Mode",
             { "Ping-Pong", "Dub", "Warp" }, dlyModeAttachment);

    for (auto* b : { &fxOnButton, &limiterButton, &spaceOnButton })
    {
        b->setColour(juce::ToggleButton::textColourId, kSub);
        content.addAndMakeVisible(*b);
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
    content.addAndMakeVisible(loadButton);

    // Rearrange the break. The slice COUNT (4/8/16/32/64) decides how many pieces there are
    // to shuffle, and the permutation stays inside the slice boundaries — so the beat is
    // rearranged at the cue points rather than chopped somewhere arbitrary.
    shuffleButton.onClick = [this]
    {
        auto& layer = processorRef.getSampleLayer();
        const int count = layer.getNumSlices();
        if (count <= 1) return;      // nothing to rearrange
        juce::Random rng;
        layer.setSliceOrder(Nebula2::shuffledSliceOrder(count, rng));
        waveform.repaint();
    };
    content.addAndMakeVisible(shuffleButton);

    // The "best options" arrangement: it listens to what each slice IS (kick / snare / hat)
    // and puts drums where drums belong, rather than scattering them. Falls back to a plain
    // shuffle when the analysis finds no drums — better an honest shuffle than a
    // rearrangement that claims to be musical and isn't.
    suggestBeatButton.onClick = [this]
    {
        auto& layer = processorRef.getSampleLayer();
        const int count = layer.getNumSlices();
        if (count <= 1) return;

        const auto info = layer.analyseCurrentSlices();   // message thread: this allocates
        juce::Random rng;
        layer.setSliceOrder(Nebula2::musicalSliceOrder(info, count, rng));
        waveform.repaint();
        refreshSampleInfo();
    };
    content.addAndMakeVisible(suggestBeatButton);

    resetOrderButton.onClick = [this]
    {
        processorRef.getSampleLayer().resetSliceOrder();
        waveform.repaint();
    };
    content.addAndMakeVisible(resetOrderButton);

    sampleInfo.setColour(juce::Label::textColourId, kSub);
    sampleInfo.setJustificationType(juce::Justification::centredLeft);
    content.addAndMakeVisible(sampleInfo);

    addCombo(sliceModeBox, sliceModeLabel, Nebula2::ParamID::sliceMode, "Slice",
             { "Grid", "Transient" }, sliceModeAttachment);
    addCombo(sliceCountBox, sliceCountLabel, Nebula2::ParamID::sliceCount, "Count",
             { "4", "8", "16", "32", "64" }, sliceCountAttachment);
    addKnob(sensitivity, Nebula2::ParamID::sensitivity, "Sens", "");
    sensitivity.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivity.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    sensitivity.label.setJustificationType(juce::Justification::centredLeft);
    sensitivity.slider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    sensitivity.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    sensitivity.slider.updateText();

    // --- layer mixer ---
    addKnob(smpVol, Nebula2::ParamID::smpVol, "Sample", " %");
    addKnob(drmVol, Nebula2::ParamID::drmVol, "Drums",  " %");
    addCombo(soloBox, soloLabel, Nebula2::ParamID::soloLayer, "Solo",
             { "Off", "Sample", "Drums" }, soloAttachment);

    content.addAndMakeVisible(waveform);

    // --- FX grid ---
    content.addAndMakeVisible(gridView);
    gridOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    content.addAndMakeVisible(gridOnButton);
    gridOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::gridOn, gridOnButton);
    addCombo(gridStepsBox, gridStepsLabel, Nebula2::ParamID::gridSteps, "Steps",
             { "8", "16", "32" }, gridStepsAttachment);
    gridClearButton.onClick = [this] { processorRef.getGrid().clearAll(); gridView.repaint(); };
    content.addAndMakeVisible(gridClearButton);

    // The dice. Density is a plain combo, not a parameter — automating "how random" would
    // change nothing, since the roll only happens on a click.
    gridDiceLabel.setText("Density", juce::dontSendNotification);
    gridDiceLabel.setColour(juce::Label::textColourId, kSub);
    content.addAndMakeVisible(gridDiceLabel);
    gridDiceBox.addItemList({ "Low", "Mid", "High" }, 1);
    gridDiceBox.setSelectedId(processorRef.getGridDiceDensity() + 1, juce::dontSendNotification);
    gridDiceBox.onChange = [this] { processorRef.setGridDiceDensity(gridDiceBox.getSelectedId() - 1); };
    content.addAndMakeVisible(gridDiceBox);

    gridDiceButton.onClick = [this] { rollGrid(); };
    content.addAndMakeVisible(gridDiceButton);

    // Pattern menu: the factory patterns, then anything you've saved.
    gridPatternLabel.setText("Pattern", juce::dontSendNotification);
    gridPatternLabel.setColour(juce::Label::textColourId, kSub);
    content.addAndMakeVisible(gridPatternLabel);
    content.addAndMakeVisible(gridPatternBox);
    refreshGridPatternMenu();
    gridPatternBox.onChange = [this]
    {
        const auto name = gridPatternBox.getText();
        if (name.isEmpty()) return;
        auto& grid = processorRef.getGrid();
        // A saved pattern wins over a factory one of the same name — it's the one YOU made.
        if (! Nebula2::loadGridPreset(name, grid))
            Nebula2::applyBuiltInGridPattern(name, grid);
        gridView.repaint();
    };

    gridSaveButton.onClick  = [this] { saveGridPatternAs(); };
    gridDeleteButton.onClick = [this]
    {
        const auto name = gridPatternBox.getText();
        // Only your own patterns can be deleted — the factory ones are code, not files, so
        // "delete" on one would silently do nothing.
        if (name.isEmpty() || ! Nebula2::listGridPresets().contains(name)) return;
        Nebula2::deleteGridPreset(name);
        refreshGridPatternMenu();
    };
    content.addAndMakeVisible(gridSaveButton);
    content.addAndMakeVisible(gridDeleteButton);

    // --- Morph pad ---
    content.addAndMakeVisible(morphPad);
    padOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    content.addAndMakeVisible(padOnButton);
    padOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::padOn, padOnButton);
    addCombo(morphMotionBox, morphMotionLabel, Nebula2::ParamID::morphMotion, "Motion",
             { "Off", "Circle", "Fig-8", "Square", "Drift" }, morphMotionAttachment);
    addCombo(morphRateBox, morphRateLabel, Nebula2::ParamID::morphRate, "Every",
             { "1 bar", "2 bars", "4 bars", "8 bars" }, morphRateAttachment);
    addKnob(morphSize,  Nebula2::ParamID::morphSize,  "Size",  " %");
    addKnob(morphGlide, Nebula2::ParamID::morphGlide, "Glide", " %");

    // The pad has no scene editor, so this is the only way to change what it morphs
    // BETWEEN. Without it the four corners sit at their seed values forever, which is why
    // "what does the morph actually morph?" is a hard question to answer from the UI.
    morphRandButton.onClick = [this]
    {
        juce::Random rng;
        processorRef.getMorphScenes() = Nebula2::randomMorphScenes(rng);
        // Rolling scenes while the pad is off would change nothing audible, which reads as
        // a broken button — so switch it on, as the prototype does.
        if (auto* p = processorRef.getValueTreeState().getParameter(Nebula2::ParamID::padOn))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(1.0f);
            p->endChangeGesture();
        }
        morphPad.repaint();
    };
    content.addAndMakeVisible(morphRandButton);

    // --- Modular rack ---
    content.addAndMakeVisible(rackView);
    rackOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    content.addAndMakeVisible(rackOnButton);
    rackOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::rackOn, rackOnButton);

    rackClearButton.onClick = [this] { rackView.clearPatch(); };
    content.addAndMakeVisible(rackClearButton);

    // Only the dials you'd reach for while patching are on the panel; the rest are still
    // full host parameters, so nothing here is a control the DAW can't see.
    // The rack's dials live ON its modules now (RackView::buildModuleDials), and their
    // readouts live on the PARAMETERS (Parameters.cpp) so the host's automation lane reads
    // properly too — not just this editor.

    addCombo(fltTypeBox, fltTypeLabel, Nebula2::ParamID::fltType, "Filter",
             { "Low Pass", "Band Pass", "High Pass" }, fltTypeAttachment);
    addCombo(lfoShapeBox, lfoShapeLabel, Nebula2::ParamID::lfoShape, "LFO",
             { "Sine", "Triangle", "Saw", "Square" }, lfoShapeAttachment);

    content.onPaint   = [this](juce::Graphics& g) { paintContent(g); };
    content.onResized = [this] { layoutContent(); };
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    // --- audition (in-app Play) ---
    auditionButton.onClick = [this]
    {
        const bool nowOn = ! processorRef.isAuditioning();
        processorRef.setAudition(nowOn);
        auditionButton.setButtonText(nowOn ? juce::String::fromUTF8("■ Stop")
                                           : juce::String::fromUTF8("▶ Play"));
    };
    addAndMakeVisible(auditionButton);

    // --- tabs ---
    // First tab is "SAMPLE", not "PLAY": it selects the sample/colour/space PAGE, and a tab
    // labelled PLAY read like an audition button (that's now the real ▶ Play in the header).
    //
    // Tab ORDER is presentation and lives in tabOrder(); the Page enum is identity. Reading
    // the enum as the running order is what made "put GRID before MORPH" look like a change
    // to the page identities rather than to a list of four entries.
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& t = tabs[i];
        const Page p = tabOrder()[i];
        t.setButtonText(tabName(p));
        t.setClickingTogglesState(false);
        t.onClick = [this, p] { showPage(p); };
        addAndMakeVisible(t);
    }

    refreshSampleInfo();
    updateSliceControlStates();
    startTimerHz(8);        // watch for slice-mode changes (incl. from host automation)

    // --- zoom ---
    // The OS scale is a good guess, not a fact: a plugin can end up on a second monitor
    // with different scaling, and eyes differ. So it's a guess you can overrule.
    zoomBox.addItemList({ "100%", "125%", "150%", "175%", "200%" }, 1);
    zoomBox.onChange = [this]
    {
        static const float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
        const int i = juce::jlimit(0, 4, zoomBox.getSelectedItemIndex());
        uiScale = scales[i];
        processorRef.setUiScale(uiScale);      // remembered with the song
        applyScale();
    };
    addAndMakeVisible(zoomBox);

    // A scale saved with the session wins; otherwise follow the screen.
    uiScale = processorRef.getUiScale() > 0.0f ? processorRef.getUiScale() : defaultUiScale();
    {
        static const float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
        int best = 0;
        for (int i = 0; i < 5; ++i)
            if (std::abs(scales[i] - uiScale) < std::abs(scales[best] - uiScale)) best = i;
        uiScale = scales[best];
        zoomBox.setSelectedItemIndex(best, juce::dontSendNotification);
    }

    setResizable(true, true);
    setResizeLimits(620, 300, 1100, 1000);
    setSize(680, 700);
    showPage(Page::play);        // sizes the window to the page
    applyScale();
}

float Nebula2AudioProcessorEditor::defaultUiScale()
{
    // Whatever the OS is scaling everything else by is what the browser was scaling the
    // prototype by. Start there — it reproduces what the prototype actually looked like on
    // this screen, rather than what its stylesheet says in the abstract.
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::jlimit(1.0f, 2.0f, (float) d->scale);
    return 1.25f;
}

void Nebula2AudioProcessorEditor::applyScale()
{
    // The layout keeps working in the prototype's units; the whole surface is scaled on
    // the way to the screen. One transform beats touching 40 font sizes and every bound
    // that depends on them.
    setTransform(juce::AffineTransform::scale(uiScale));
    setSize(getWidth(), getHeight());     // re-reports the scaled size to the host
    resized();
    repaint();
}

Nebula2AudioProcessorEditor::~Nebula2AudioProcessorEditor()
{
    // A LookAndFeel must outlive every component using it. It's a member declared after
    // the children, so clear it here rather than discovering the order the hard way.
    setLookAndFeel(nullptr);
}

// The grid page is exactly as tall as its lanes need. The old hardcoded 194px was chosen
// when there were seven lanes; at sixteen it gave each one under 8px and the labels
// collided into a smear.
//   panel = 14 title + 24 controls + 8 gap + lanes, plus 24 for the panel's own inset
//   page  = panel + 16 for the body inset
void Nebula2AudioProcessorEditor::refreshGridPatternMenu()
{
    const auto keep = gridPatternBox.getText();
    gridPatternBox.clear(juce::dontSendNotification);

    // Yours first, under a heading, because they're the ones you'll be looking for.
    const auto mine = Nebula2::listGridPresets();
    int id = 1;
    if (! mine.isEmpty())
    {
        gridPatternBox.addSectionHeading("Saved");
        for (const auto& n : mine) gridPatternBox.addItem(n, id++);
    }
    gridPatternBox.addSectionHeading("Factory");
    for (const auto& n : Nebula2::builtInGridPatternNames()) gridPatternBox.addItem(n, id++);

    // Keep the selection across a refresh if it still exists.
    for (int i = 0; i < gridPatternBox.getNumItems(); ++i)
        if (gridPatternBox.getItemText(i) == keep)
        {
            gridPatternBox.setSelectedItemIndex(i, juce::dontSendNotification);
            return;
        }
    gridPatternBox.setText({}, juce::dontSendNotification);
}

void Nebula2AudioProcessorEditor::saveGridPatternAs()
{
    auto* aw = new juce::AlertWindow("Save grid pattern",
                                     "Saved to your Nebula2 folder, so it's there in every "
                                     "project — not just this one.",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", gridPatternBox.getText(), "Name");
    aw->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, aw](int result)
        {
            std::unique_ptr<juce::AlertWindow> owned(aw);
            if (result != 1) return;

            const auto typed = owned->getTextEditorContents("name");
            const auto safe  = Nebula2::sanitiseGridPresetName(typed);
            if (safe.isEmpty())
            {
                juce::NativeMessageBox::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("That name won't work")
                        .withMessage("A pattern name has to survive becoming a filename. "
                                     "Try letters and numbers."),
                    nullptr);
                return;
            }

            if (! Nebula2::saveGridPreset(safe, processorRef.getGrid()))
            {
                // Say it failed. A save button that silently does nothing is how you lose
                // work and only find out later.
                juce::NativeMessageBox::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Couldn't save")
                        .withMessage("Writing to " + Nebula2::gridPresetDirectory().getFullPathName()
                                     + " failed."),
                    nullptr);
                return;
            }

            refreshGridPatternMenu();
            for (int i = 0; i < gridPatternBox.getNumItems(); ++i)
                if (gridPatternBox.getItemText(i) == safe)
                    gridPatternBox.setSelectedItemIndex(i, juce::dontSendNotification);
        }), false);
}

void Nebula2AudioProcessorEditor::rollGrid()
{
    // Only offer the dice lanes that can actually SOUND. A lane sitting on its neutral
    // can't be heard however it's painted, so casting one wastes a slot and makes the roll
    // quietly sparser than the density you asked for. This is the same at-rest rule the
    // view greys lanes with — read from the live parameter values, which is why the filter
    // happens here rather than inside randomiseGrid().
    std::vector<Nebula2::GridRow> eligible;
    auto& vts = processorRef.getValueTreeState();
    for (auto r : Nebula2::gridDisplayOrder())
    {
        const char* id = Nebula2::gridRowPanelParamId(r);
        if (id == nullptr) continue;
        auto* raw = vts.getRawParameterValue(id);
        if (raw == nullptr) continue;
        if (! Nebula2::gridRowIsAtRest(r, raw->load())) eligible.push_back(r);
    }

    if (eligible.empty())
    {
        // Nothing could sound, so rolling would paint a grid you can't hear. Say why rather
        // than appearing to work — the same law the greyed lanes follow.
        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("Nothing to randomise yet")
                .withMessage("Every lane is sitting at its resting value, so a pattern "
                             "wouldn't be audible.\n\nTurn up a lane's knob on the SAMPLE "
                             "page first — Drive, Stutter, Reverb, whatever you want the "
                             "dice to play with."),
            nullptr);
        return;
    }

    juce::Random rng;   // time-seeded: a fresh roll each click
    Nebula2::randomiseGrid(processorRef.getGrid(), eligible,
                           (Nebula2::RandomDensity) processorRef.getGridDiceDensity(), rng);

    // A pattern you can't hear looks like a broken button, so the roll also switches the
    // grid on (the prototype does the same).
    if (auto* p = vts.getParameter(Nebula2::ParamID::gridOn))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(1.0f);
        p->endChangeGesture();
    }

    gridView.repaint();
}

int Nebula2AudioProcessorEditor::gridPageHeight()
{
    // 14 title + 24 controls row 1 + 6 + 24 controls row 2 + 8 gap + lanes, + 24 panel
    // inset + 16 body inset.
    return GridView::preferredHeight() + 14 + 24 + 6 + 24 + 8 + 24 + 16;
}

int Nebula2AudioProcessorEditor::contentHeightFor(Page p) const
{
    switch (p)
    {
        case Page::play:  return 822;   // sample + mixer + colour (two knob rows) + space
        case Page::morph: return 320;
        case Page::grid:  return gridPageHeight();
        case Page::rack:  return 470;
        default:          return 560;
    }
}

void Nebula2AudioProcessorEditor::showPage(Page p)
{
    page = p;

    // Highlight by POSITION in the running order, not by enum value — those are no longer
    // the same number.
    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setToggleState(tabOrder()[i] == p, juce::dontSendNotification);

    // Only the current page's controls exist on screen. Hiding rather than rebuilding
    // keeps every attachment live, so a knob on a hidden page still tracks host
    // automation and is correct the instant you switch to it.
    const bool play  = p == Page::play;
    const bool morph = p == Page::morph;
    const bool grid  = p == Page::grid;
    const bool rack  = p == Page::rack;

    juce::Component* playChildren[] = {
        &loadButton, &shuffleButton, &suggestBeatButton, &resetOrderButton, &sampleInfo, &waveform,
        &sliceModeBox, &sliceCountBox, &sliceModeLabel, &sliceCountLabel,
        &charBox, &charLabel, &revCharBox, &revCharLabel,
        &dlySyncBox, &dlySyncLabel, &dlyModeBox, &dlyModeLabel, &fxOnButton, &limiterButton, &spaceOnButton,
        &resoKeyBox, &resoKeyLabel, &resoScaleBox, &resoScaleLabel,
        &colourRandButton, &spaceRandButton, &soloBox, &soloLabel
    };
    for (auto* c : playChildren) c->setVisible(play);

    for (auto* k : { &drive, &crush, &squeeze, &tone, &width, &pump, &master,
                     &revMix, &revSize, &dlyMix, &dlyFb, &haunt, &sensitivity,
                     &smpVol, &drmVol })
    {
        k->slider.setVisible(play);
        k->label.setVisible(play);
    }

    // The lane knobs, by iterating the container that HOLDS them rather than naming them
    // again. Widgets built in a loop but hidden by a hand-written list is how seven stray
    // "0 %" readouts came to float beneath the GRID page: they were never added to that
    // list, so they stayed visible on every page wearing the play page's stale bounds.
    // Looping the vector makes the two lists incapable of disagreeing.
    for (auto& k : laneKnobs)
    {
        k->slider.setVisible(play);
        k->label.setVisible(play);
    }

    morphPad.setVisible(morph);
    padOnButton.setVisible(morph);
    morphMotionBox.setVisible(morph); morphMotionLabel.setVisible(morph);
    morphRateBox.setVisible(morph);   morphRateLabel.setVisible(morph);
    for (auto* k : { &morphSize, &morphGlide }) { k->slider.setVisible(morph); k->label.setVisible(morph); }
    morphRandButton.setVisible(morph);

    gridView.setVisible(grid);
    gridOnButton.setVisible(grid);
    gridStepsBox.setVisible(grid);
    gridStepsLabel.setVisible(grid);
    gridClearButton.setVisible(grid);
    gridDiceButton.setVisible(grid);
    gridDiceBox.setVisible(grid);
    gridDiceLabel.setVisible(grid);
    gridPatternBox.setVisible(grid);
    gridPatternLabel.setVisible(grid);
    gridSaveButton.setVisible(grid);
    gridDeleteButton.setVisible(grid);

    rackView.setVisible(rack);
    rackOnButton.setVisible(rack);
    rackClearButton.setVisible(rack);
    fltTypeBox.setVisible(rack);
    fltTypeLabel.setVisible(rack);
    lfoShapeBox.setVisible(rack);
    lfoShapeLabel.setVisible(rack);

    if (play) updateSliceControlStates();   // law 4 still applies on the page you're on

    // Fit the window to the page. Without this, MORPH and GRID leave most of the window
    // empty and PLAY leaves a dead band under SPACE — the window was sized for the tallest
    // page and every other one paid for it. Height only: a width the user chose is theirs.
    setSize(getWidth(), juce::jlimit(460, 1000, contentHeightFor(p) + headerH + 16));

    resized();
    repaint();
}

void Nebula2AudioProcessorEditor::timerCallback()
{
    updateSliceControlStates();

    // Keep the Play button honest: the host transport can clear audition on its own (it
    // "takes over"), so the button must follow the processor's real state, not the last
    // click.
    const bool on = processorRef.isAuditioning();
    const auto want = on ? juce::String::fromUTF8("■ Stop") : juce::String::fromUTF8("▶ Play");
    if (auditionButton.getButtonText() != want)
        auditionButton.setButtonText(want);
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
    content.addAndMakeVisible(k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, kSub);
    content.addAndMakeVisible(k.label);

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
    content.addAndMakeVisible(label);

    box.addItemList(items, 1);
    content.addAndMakeVisible(box);
    attachment = std::make_unique<APVTS::ComboBoxAttachment>(processorRef.getValueTreeState(), paramID, box);
}

void Nebula2AudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace Nebula2;

    // A vertical wash rather than a flat fill — it's what stops a dark plugin reading as a
    // black hole. Straight from the prototype's body gradient.
    juce::ColourGradient bgGrad(juce::Colour(0xff131a30), 0.0f, 0.0f,
                                Theme::bg, 0.0f, (float) getHeight(), false);
    bgGrad.addColour(0.55, juce::Colour(0xff0d1222));
    g.setGradientFill(bgGrad);
    g.fillAll();

    auto header = getLocalBounds().removeFromTop(headerH);

    auto title = header.reduced(16, 8).removeFromTop(24);
    title.removeFromRight(100);   // the Play button lives here (placed in resized())
    g.setColour(Theme::ink);
    g.setFont(Theme::ui(19.0f, true));
    g.drawFittedText("Nebula2", title.removeFromLeft(96), juce::Justification::centredLeft, 1);

    g.setColour(Theme::faint);
    g.setFont(Theme::mono(10.0f));
    g.drawFittedText("drums 36-46, 75   |   B4 whole break   |   C5+ slices",
                     title, juce::Justification::centredLeft, 1);

    g.setColour(Theme::line);
    g.drawLine(0.0f, (float) headerH - 0.5f, (float) getWidth(), (float) headerH - 0.5f);
}

void Nebula2AudioProcessorEditor::paintContent(juce::Graphics& g)
{
    using namespace Nebula2;
    g.fillAll(juce::Colours::transparentBlack);

    auto body = content.getLocalBounds().reduced(12, 8).toFloat();

    if (page == Page::play)
    {
        auto sampleArea = body.removeFromTop(222.0f);
        Nebula2LookAndFeel::drawCard(g, sampleArea, "SAMPLE");
        if (dragHighlight)
        {
            g.setColour(Theme::teal.withAlpha(0.18f));
            g.fillRoundedRectangle(sampleArea, Theme::cardRadius);
            g.setColour(Theme::teal);
            g.drawRoundedRectangle(sampleArea.reduced(1.0f), Theme::cardRadius, 1.5f);
        }

        body.removeFromTop(10.0f);
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(340.0f), "COLOUR");
        body.removeFromTop(10.0f);

        // SPACE, split into REVERB and DELAY sub-sections (the user's request). One card,
        // two labelled halves — a divider makes the separation read at a glance.
        auto spaceCard = body.removeFromTop(214.0f);
        Nebula2LookAndFeel::drawCard(g, spaceCard, "SPACE");
        auto inner = spaceCard.reduced(14.0f, 4.0f);
        inner.removeFromTop(16.0f);                        // clear the SPACE title
        g.setColour(Theme::teal.withAlpha(0.85f));
        g.setFont(Theme::ui(9.0f, true));
        g.drawText("REVERB", inner.removeFromTop(14.0f), juce::Justification::topLeft);
        auto delayHalf = inner.withTop(inner.getY() + 92.0f);
        g.setColour(Theme::line);
        g.drawLine(delayHalf.getX(), delayHalf.getY() - 4.0f,
                   delayHalf.getRight(), delayHalf.getY() - 4.0f);   // divider
        g.setColour(Theme::teal.withAlpha(0.85f));
        g.drawText("DELAY", delayHalf.removeFromTop(14.0f), juce::Justification::topLeft);
    }
    else if (page == Page::morph)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(300.0f), "MORPH");
    }
    else if (page == Page::grid)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop((float) (gridPageHeight() - 16)), "GRID");
    }
    else if (page == Page::rack)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(454.0f), "RACK");
    }
}

void Nebula2AudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(headerH);

    // The in-app Play button sits in the title row, top-right (paint() reserves the space).
    auditionButton.setBounds(header.reduced(16, 8).removeFromTop(24).removeFromRight(92));

    // Tabs sit under the title.
    auto tabRow = header.withTrimmedTop(38).reduced(14, 4);
    zoomBox.setBounds(tabRow.removeFromRight(66).reduced(0, 2));
    tabRow.removeFromRight(10);

    const int tw = juce::jmin(84, tabRow.getWidth() / (int) tabs.size());
    for (auto& t : tabs)
    {
        t.setBounds(tabRow.removeFromLeft(tw).reduced(2, 0));
        tabRow.removeFromLeft(2);
    }

    viewport.setBounds(r);
    const int sbW = viewport.isVerticalScrollBarShown() ? viewport.getScrollBarThickness() : 0;
    content.setSize(juce::jmax(560, r.getWidth() - sbW), contentHeightFor(page));
}

void Nebula2AudioProcessorEditor::layoutContent()
{
    auto body = content.getLocalBounds().reduced(12, 8);

    if (page != Page::play)
    {
        // --- Morph page ---
        if (page == Page::morph)
        {
            auto mp = body.removeFromTop(300).reduced(12);
            mp.removeFromTop(14);

            // Top row: Morph On, then the auto-motion controls (Motion, Every, Size, Glide).
            auto row = mp.removeFromTop(24);
            padOnButton.setBounds(row.removeFromLeft(100));
            row.removeFromLeft(10);
            morphMotionLabel.setBounds(row.removeFromLeft(44));
            morphMotionBox.setBounds(row.removeFromLeft(88).reduced(0, 1));
            row.removeFromLeft(10);
            morphRateLabel.setBounds(row.removeFromLeft(40));
            morphRateBox.setBounds(row.removeFromLeft(86).reduced(0, 1));
            row.removeFromLeft(12);
            morphRandButton.setBounds(row.removeFromLeft(96));

            mp.removeFromTop(6);
            auto krow = mp.removeFromTop(20);
            morphSize.label.setBounds(krow.removeFromLeft(36));
            morphSize.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            morphSize.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
            morphSize.slider.setBounds(krow.removeFromLeft(160));
            krow.removeFromLeft(16);
            morphGlide.label.setBounds(krow.removeFromLeft(40));
            morphGlide.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            morphGlide.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
            morphGlide.slider.setBounds(krow.removeFromLeft(160));

            mp.removeFromTop(8);
            morphPad.setBounds(mp);
        }
        // --- Grid page ---
        else if (page == Page::grid)
        {
            auto gp = body.removeFromTop(gridPageHeight() - 16).reduced(12);
            gp.removeFromTop(14);
            auto gRow = gp.removeFromTop(24);
            gridOnButton.setBounds(gRow.removeFromLeft(96));
            gRow.removeFromLeft(10);
            gridStepsLabel.setBounds(gRow.removeFromLeft(40));
            gridStepsBox.setBounds(gRow.removeFromLeft(66));
            gRow.removeFromLeft(12);
            gridClearButton.setBounds(gRow.removeFromLeft(62));

            // Second row. Eight controls do not fit on one line at this width, and
            // removeFromLeft silently hands back whatever is left rather than failing — so
            // an overflowing row doesn't error, it just draws a squashed control you can't
            // read. (That's how "Space On" came out truncated once already.)
            gp.removeFromTop(6);
            auto gRow2 = gp.removeFromTop(24);
            gridDiceButton.setBounds(gRow2.removeFromLeft(84));
            gRow2.removeFromLeft(6);
            gridDiceLabel.setBounds(gRow2.removeFromLeft(50));
            gridDiceBox.setBounds(gRow2.removeFromLeft(66));
            gRow2.removeFromLeft(14);
            gridPatternLabel.setBounds(gRow2.removeFromLeft(48));
            gridPatternBox.setBounds(gRow2.removeFromLeft(140));
            gRow2.removeFromLeft(6);
            gridSaveButton.setBounds(gRow2.removeFromLeft(54));
            gRow2.removeFromLeft(4);
            gridDeleteButton.setBounds(gRow2.removeFromLeft(60));

            gp.removeFromTop(8);
            gridView.setBounds(gp);
        }
        // --- Rack page ---
        else if (page == Page::rack)
        {
            auto rp = body.removeFromTop(454).reduced(12);
            rp.removeFromTop(14);
            auto rRow = rp.removeFromTop(24);
            rackOnButton.setBounds(rRow.removeFromLeft(96));
            rRow.removeFromLeft(8);
            rackClearButton.setBounds(rRow.removeFromLeft(86));
            rRow.removeFromLeft(12);
            fltTypeLabel.setBounds(rRow.removeFromLeft(38));
            fltTypeBox.setBounds(rRow.removeFromLeft(86));
            rRow.removeFromLeft(8);
            lfoShapeLabel.setBounds(rRow.removeFromLeft(28));
            lfoShapeBox.setBounds(rRow.removeFromLeft(80));

            rp.removeFromTop(6);
            // The dials now live ON the modules (see RackView::buildModuleDials), so the
            // rack gets the whole panel. A shared row at the bottom made you map "Comb FB"
            // back to a box by memory; on the box, the dial IS the module.
            rackView.setBounds(rp);
        }
        return;
    }

    // --- Sample panel ---
    auto sampleArea = body.removeFromTop(222).reduced(10);
    sampleArea.removeFromTop(12);
    auto sRow2 = sampleArea.removeFromTop(26);
    loadButton.setBounds(sRow2.removeFromLeft(120));
    sRow2.removeFromLeft(10);
    shuffleButton.setBounds(sRow2.removeFromLeft(70));
    sRow2.removeFromLeft(4);
    suggestBeatButton.setBounds(sRow2.removeFromLeft(74));
    sRow2.removeFromLeft(4);
    resetOrderButton.setBounds(sRow2.removeFromLeft(86));
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
    // Sens is a slider in a 34px row: as a rotary it had no room for a dial at all and
    // rendered as a bare "50 %". A horizontal slider is the honest shape for this cell.
    sensitivity.label.setBounds(sliceRow.removeFromLeft(38));
    sensitivity.slider.setBounds(sliceRow.removeFromLeft(150).reduced(0, 6));

    // Layer mixer: sits with the sample controls because the balance you want is the one
    // between what you just loaded and the kit under it.
    sampleArea.removeFromTop(4);
    auto mixRow = sampleArea.removeFromTop(30);
    smpVol.label.setBounds(mixRow.removeFromLeft(52));
    smpVol.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    smpVol.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
    smpVol.slider.setBounds(mixRow.removeFromLeft(150).reduced(0, 4));
    mixRow.removeFromLeft(12);
    drmVol.label.setBounds(mixRow.removeFromLeft(48));
    drmVol.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    drmVol.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
    drmVol.slider.setBounds(mixRow.removeFromLeft(150).reduced(0, 4));
    mixRow.removeFromLeft(12);
    soloLabel.setBounds(mixRow.removeFromLeft(34));
    soloBox.setBounds(mixRow.removeFromLeft(86).reduced(0, 3));

    sampleArea.removeFromTop(4);
    waveform.setBounds(sampleArea);
    body.removeFromTop(8);

    // --- Colour panel ---
    auto colour = body.removeFromTop(340).reduced(10);
    colour.removeFromTop(12);
    auto knobRow = colour.removeFromTop(120);
    Knob* cKnobs[] = { &drive, &crush, &squeeze, &tone, &width, &pump, &master };
    const int cw = knobRow.getWidth() / 7;   // drive/crush/squeeze/tone/width/pump/master
    for (auto* k : cKnobs)
    {
        auto cell = knobRow.removeFromLeft(cw);
        k->label.setBounds(cell.removeFromTop(16));
        k->slider.setBounds(cell.reduced(3));
    }

    // Second row: the lane knobs. Same count as the row above (7), so they share the
    // column width and the two rows line up rather than looking like a bolted-on strip.
    colour.removeFromTop(8);
    auto laneRow = colour.removeFromTop(110);
    if (! laneKnobs.empty())
    {
        const int lw = laneRow.getWidth() / (int) laneKnobs.size();
        for (auto& k : laneKnobs)
        {
            auto cell = laneRow.removeFromLeft(lw);
            k->label.setBounds(cell.removeFromTop(16));
            k->slider.setBounds(cell.reduced(3));
        }
    }

    // Resonate's Key and Scale sit under it — they only mean anything with Resonate up.
    colour.removeFromTop(6);
    auto rRow2 = colour.removeFromTop(24);
    resoKeyLabel.setBounds(rRow2.removeFromLeft(30));
    resoKeyBox.setBounds(rRow2.removeFromLeft(62).reduced(0, 1));
    rRow2.removeFromLeft(12);
    resoScaleLabel.setBounds(rRow2.removeFromLeft(40));
    resoScaleBox.setBounds(rRow2.removeFromLeft(100).reduced(0, 1));

    colour.removeFromTop(8);
    auto cRow = colour.removeFromTop(24);
    charLabel.setBounds(cRow.removeFromLeft(64));
    charBox.setBounds(cRow.removeFromLeft(100).reduced(0, 1));
    cRow.removeFromLeft(16);
    fxOnButton.setBounds(cRow.removeFromLeft(90));
    limiterButton.setBounds(cRow.removeFromLeft(90));
    cRow.removeFromLeft(10);
    colourRandButton.setBounds(cRow.removeFromLeft(96));

    // --- Space panel: REVERB section over DELAY section ---
    body.removeFromTop(8);
    auto sp = body.removeFromTop(214).reduced(14, 4);
    sp.removeFromTop(16);   // SPACE card title

    auto layoutKnob = [](juce::Rectangle<int> cell, Knob& k)
    {
        k.label.setBounds(cell.removeFromTop(15));
        k.slider.setBounds(cell.reduced(3));
    };

    // REVERB: Mix, Size knobs + Character dropdown.
    sp.removeFromTop(14);   // "REVERB" sub-label
    auto revRow = sp.removeFromTop(74);
    layoutKnob(revRow.removeFromLeft(84), revMix);
    layoutKnob(revRow.removeFromLeft(84), revSize);
    revRow.removeFromLeft(14);
    auto revRight = revRow.withTrimmedTop(20);
    revCharLabel.setBounds(revRight.removeFromLeft(56).removeFromTop(24));
    revCharBox.setBounds(revRight.removeFromLeft(120).removeFromTop(24).reduced(0, 1));

    sp.removeFromTop(4);    // divider gap

    // DELAY: Mix, Feedback, Haunt knobs + Sync/Mode dropdowns + Space On.
    // (Haunt is a Space element in the prototype's own grouping: "reverb - delay - haunt".)
    sp.removeFromTop(14);   // "DELAY" sub-label
    auto dlyRow = sp.removeFromTop(74);
    layoutKnob(dlyRow.removeFromLeft(78), dlyMix);
    layoutKnob(dlyRow.removeFromLeft(78), dlyFb);
    layoutKnob(dlyRow.removeFromLeft(78), haunt);
    dlyRow.removeFromLeft(12);
    auto dlyRight = dlyRow.withTrimmedTop(8);
    auto dRowA = dlyRight.removeFromTop(24);
    dlySyncLabel.setBounds(dRowA.removeFromLeft(38));
    dlySyncBox.setBounds(dRowA.removeFromLeft(72).reduced(0, 1));
    dRowA.removeFromLeft(12);
    dlyModeLabel.setBounds(dRowA.removeFromLeft(40));
    dlyModeBox.setBounds(dRowA.removeFromLeft(96).reduced(0, 1));

    dlyRight.removeFromTop(6);
    auto dRowB = dlyRight.removeFromTop(24);
    spaceOnButton.setBounds(dRowB.removeFromLeft(110));
    dRowB.removeFromLeft(10);
    spaceRandButton.setBounds(dRowB.removeFromLeft(96));
}
