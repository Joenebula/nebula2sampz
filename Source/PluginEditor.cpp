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
    addKnob(drive,   Nebula2::ParamID::drive,   "Drive");
    addKnob(crush,   Nebula2::ParamID::crush,   "Crush");
    addKnob(squeeze, Nebula2::ParamID::squeeze, "Squeeze");
    addKnob(tone,    Nebula2::ParamID::tone,    "Tone");
    addKnob(width,   Nebula2::ParamID::width,   "Width");
    addKnob(master,  Nebula2::ParamID::master,  "Master");

    charLabel.setText("Character", juce::dontSendNotification);
    charLabel.setJustificationType(juce::Justification::centredLeft);
    charLabel.setColour(juce::Label::textColourId, kSub);
    addAndMakeVisible(charLabel);

    charBox.addItemList({ "Tube", "Fuzz", "Fold" }, 1);
    addAndMakeVisible(charBox);
    charAttachment = std::make_unique<APVTS::ComboBoxAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::driveChar, charBox);

    fxOnButton.setColour(juce::ToggleButton::textColourId, kSub);
    addAndMakeVisible(fxOnButton);
    fxOnAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::fxOn, fxOnButton);

    limiterButton.setColour(juce::ToggleButton::textColourId, kSub);
    addAndMakeVisible(limiterButton);
    limiterAttachment = std::make_unique<APVTS::ButtonAttachment>(
        processorRef.getValueTreeState(), Nebula2::ParamID::limiter, limiterButton);

    setSize(560, 300);
}

void Nebula2AudioProcessorEditor::addKnob(Knob& k, const juce::String& paramID, const juce::String& name)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour(juce::Slider::thumbColourId, kAccent);
    k.slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, kSub);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<APVTS::SliderAttachment>(
        processorRef.getValueTreeState(), paramID, k.slider);
}

void Nebula2AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    auto header = getLocalBounds().removeFromTop(46).reduced(16, 8);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f));
    g.drawFittedText("Nebula2", header.removeFromLeft(120), juce::Justification::centredLeft, 1);

    g.setColour(kSub);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText("drums: 36 kick  38 snare  39 clap  42/46 hats  45 tom  37 rim  75 perc",
                     header, juce::Justification::centredLeft, 1);

    g.setColour(kPanel);
    g.fillRoundedRectangle(getLocalBounds().reduced(12).withTrimmedTop(40).toFloat(), 8.0f);
}

void Nebula2AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12).withTrimmedTop(40).reduced(10);

    auto knobRow = area.removeFromTop(140);
    Knob* knobs[] = { &drive, &crush, &squeeze, &tone, &width, &master };
    const int w = knobRow.getWidth() / 6;
    for (auto* k : knobs)
    {
        auto cell = knobRow.removeFromLeft(w);
        k->label.setBounds(cell.removeFromTop(18));
        k->slider.setBounds(cell.reduced(4));
    }

    area.removeFromTop(12);
    auto bottom = area.removeFromTop(28);
    charLabel.setBounds(bottom.removeFromLeft(70));
    charBox.setBounds(bottom.removeFromLeft(110).reduced(0, 2));
    bottom.removeFromLeft(24);
    fxOnButton.setBounds(bottom.removeFromLeft(90));
    limiterButton.setBounds(bottom.removeFromLeft(90));
}
