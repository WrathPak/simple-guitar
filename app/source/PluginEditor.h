#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

/**
    Placeholder editor: dark background + product name only.

    This is intentionally minimal. The real webview-hosted editor (React UI
    talking to the engine over the bridge schema) is a separate workstream;
    it will replace this class's contents without touching the processor.
*/
class SimpleGuitarAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SimpleGuitarAudioProcessorEditor (SimpleGuitarAudioProcessor&);
    ~SimpleGuitarAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SimpleGuitarAudioProcessor& processorRef;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessorEditor)
};
