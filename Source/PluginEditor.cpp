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
    // One look-and-feel for the whole editor, set before any child is made, so nothing
    // gets a chance to render in JUCE's default grey first.
    setLookAndFeel(&lnf);

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
    sensitivity.slider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    sensitivity.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    sensitivity.slider.updateText();

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

    // --- Morph pad ---
    content.addAndMakeVisible(morphPad);
    padOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    content.addAndMakeVisible(padOnButton);
    padOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::padOn, padOnButton);

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

    content.onPaint   = [this](juce::Graphics& g) { paintContent(g); };
    content.onResized = [this] { layoutContent(); };
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    // --- tabs ---
    const char* names[] = { "PLAY", "MORPH", "GRID", "RACK" };
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& t = tabs[i];
        t.setButtonText(names[i]);
        t.setClickingTogglesState(false);
        t.onClick = [this, i] { showPage((Page) i); };
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

int Nebula2AudioProcessorEditor::contentHeightFor(Page p) const
{
    switch (p)
    {
        case Page::play:  return 560;   // sample + colour + space
        case Page::morph: return 240;
        case Page::grid:  return 210;
        case Page::rack:  return 470;
        default:          return 560;
    }
}

void Nebula2AudioProcessorEditor::showPage(Page p)
{
    page = p;

    for (size_t i = 0; i < tabs.size(); ++i)
        tabs[i].setToggleState(i == (size_t) p, juce::dontSendNotification);

    // Only the current page's controls exist on screen. Hiding rather than rebuilding
    // keeps every attachment live, so a knob on a hidden page still tracks host
    // automation and is correct the instant you switch to it.
    const bool play  = p == Page::play;
    const bool morph = p == Page::morph;
    const bool grid  = p == Page::grid;
    const bool rack  = p == Page::rack;

    juce::Component* playChildren[] = {
        &loadButton, &sampleInfo, &waveform,
        &sliceModeBox, &sliceCountBox, &sliceModeLabel, &sliceCountLabel,
        &charBox, &charLabel, &revCharBox, &revCharLabel,
        &dlySyncBox, &dlySyncLabel, &fxOnButton, &limiterButton, &spaceOnButton
    };
    for (auto* c : playChildren) c->setVisible(play);

    for (auto* k : { &drive, &crush, &squeeze, &tone, &width, &master,
                     &revMix, &dlyMix, &dlyFb, &sensitivity })
    {
        k->slider.setVisible(play);
        k->label.setVisible(play);
    }

    morphPad.setVisible(morph);
    padOnButton.setVisible(morph);

    gridView.setVisible(grid);
    gridOnButton.setVisible(grid);
    gridStepsBox.setVisible(grid);
    gridStepsLabel.setVisible(grid);
    gridClearButton.setVisible(grid);

    rackView.setVisible(rack);
    rackOnButton.setVisible(rack);
    rackClearButton.setVisible(rack);
    fltTypeBox.setVisible(rack);
    fltTypeLabel.setVisible(rack);
    lfoShapeBox.setVisible(rack);
    lfoShapeLabel.setVisible(rack);
    for (auto* k : { &fltCut, &fltRes, &lfoRate, &lfoDepth, &cmbTune,
                     &cmbFb, &vowMorph, &echTime, &echFb, &outLvl })
    {
        k->slider.setVisible(rack);
        k->label.setVisible(rack);
    }

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
        auto sampleArea = body.removeFromTop(188.0f);
        Nebula2LookAndFeel::drawCard(g, sampleArea, "SAMPLE");
        if (dragHighlight)
        {
            g.setColour(Theme::teal.withAlpha(0.18f));
            g.fillRoundedRectangle(sampleArea, Theme::cardRadius);
            g.setColour(Theme::teal);
            g.drawRoundedRectangle(sampleArea.reduced(1.0f), Theme::cardRadius, 1.5f);
        }

        body.removeFromTop(10.0f);
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(200.0f), "COLOUR");
        body.removeFromTop(10.0f);
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(126.0f), "SPACE");
    }
    else if (page == Page::morph)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(224.0f), "MORPH");
    }
    else if (page == Page::grid)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(194.0f), "GRID");
    }
    else if (page == Page::rack)
    {
        Nebula2LookAndFeel::drawCard(g, body.removeFromTop(454.0f), "RACK");
    }
}

void Nebula2AudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    // Tabs sit under the title, in the header.
    auto tabRow = r.removeFromTop(headerH).withTrimmedTop(38).reduced(14, 4);
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
            auto mp = body.removeFromTop(224).reduced(12);
            mp.removeFromTop(14);
            padOnButton.setBounds(mp.removeFromTop(22).removeFromLeft(96));
            mp.removeFromTop(6);
            morphPad.setBounds(mp);
        }
        // --- Grid page ---
        else if (page == Page::grid)
        {
            auto gp = body.removeFromTop(194).reduced(12);
            gp.removeFromTop(14);
            auto gRow = gp.removeFromTop(24);
            gridOnButton.setBounds(gRow.removeFromLeft(82));
            gRow.removeFromLeft(10);
            gridStepsLabel.setBounds(gRow.removeFromLeft(40));
            gridStepsBox.setBounds(gRow.removeFromLeft(66));
            gRow.removeFromLeft(12);
            gridClearButton.setBounds(gRow.removeFromLeft(62));
            gp.removeFromTop(8);
            gridView.setBounds(gp);
        }
        // --- Rack page ---
        else if (page == Page::rack)
        {
            auto rp = body.removeFromTop(454).reduced(12);
            rp.removeFromTop(14);
            auto rRow = rp.removeFromTop(24);
            rackOnButton.setBounds(rRow.removeFromLeft(80));
            rRow.removeFromLeft(8);
            rackClearButton.setBounds(rRow.removeFromLeft(86));
            rRow.removeFromLeft(12);
            fltTypeLabel.setBounds(rRow.removeFromLeft(38));
            fltTypeBox.setBounds(rRow.removeFromLeft(86));
            rRow.removeFromLeft(8);
            lfoShapeLabel.setBounds(rRow.removeFromLeft(28));
            lfoShapeBox.setBounds(rRow.removeFromLeft(80));

            rp.removeFromTop(6);
            rackView.setBounds(rp.removeFromTop(rp.getHeight() - 84));

            rp.removeFromTop(6);
            auto kRow = rp;
            const int kw = kRow.getWidth() / 10;
            Knob* rackKnobs[] = { &fltCut, &fltRes, &lfoRate, &lfoDepth, &cmbTune,
                                  &cmbFb, &vowMorph, &echTime, &echFb, &outLvl };
            for (auto* k : rackKnobs)
            {
                auto cell = kRow.removeFromLeft(kw);
                k->label.setBounds(cell.removeFromTop(14));
                k->slider.setBounds(cell.reduced(2, 0));
            }
        }
        return;
    }

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
    // Sens is a slider in a 34px row: as a rotary it had no room for a dial at all and
    // rendered as a bare "50 %". A horizontal slider is the honest shape for this cell.
    sensitivity.label.setBounds(sliceRow.removeFromLeft(38));
    sensitivity.slider.setBounds(sliceRow.removeFromLeft(150).reduced(0, 6));

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
    spaceOnButton.setBounds(right.removeFromLeft(100));   // 86 truncated it to "Space..."

}
