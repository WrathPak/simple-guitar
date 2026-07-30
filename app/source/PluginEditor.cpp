#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour (0xff0a0a0c);
}

SimpleGuitarAudioProcessorEditor::SimpleGuitarAudioProcessorEditor (SimpleGuitarAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    titleLabel.setText ("Simple Guitar", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    setResizable (false, false);
    setSize (600, 400);
}

SimpleGuitarAudioProcessorEditor::~SimpleGuitarAudioProcessorEditor() = default;

void SimpleGuitarAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void SimpleGuitarAudioProcessorEditor::resized()
{
    titleLabel.setBounds (getLocalBounds());
}
