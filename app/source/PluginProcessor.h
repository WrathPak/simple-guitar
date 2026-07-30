#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <sg/Gain.h>
#include <sg/MeterTap.h>

/**
    Stereo passthrough processor: input -> sg::Gain (output trim) -> sg::MeterTap -> output.

    Single automatable parameter, "outputGain" (-60..+12 dB, default 0dB), exposed via
    AudioProcessorValueTreeState so DAW automation, preset restore, and (later) the
    webview bridge parameter attachments stay in sync for free.
*/
class SimpleGuitarAudioProcessor final : public juce::AudioProcessor
{
public:
    SimpleGuitarAudioProcessor();
    ~SimpleGuitarAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Parameter id for the output gain parameter (-60..+12 dB, default 0). */
    static constexpr const char* outputGainParamId = "outputGain";

    /** Read-only access to the post-gain meter tap, e.g. for a UI-side timer
        or (later) the webview bridge to push meterFrame events from. */
    sg::MeterTap& getMeterTap() noexcept { return meterTap; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    sg::Gain gain;
    sg::MeterTap meterTap;

    // Cached raw pointer into the APVTS parameter's atomic backing value.
    // Read on the audio thread via atomic load only — never dereferences
    // the AudioProcessorValueTreeState itself in processBlock().
    std::atomic<float>* outputGainDbParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGuitarAudioProcessor)
};
