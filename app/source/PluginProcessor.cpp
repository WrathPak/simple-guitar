#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

#include <juce_events/juce_events.h>

namespace
{
    // True logarithmic (equal-ratio) frequency mapping for the cab hi/lo cut
    // knobs, so the knob feels linear in pitch/octaves rather than linear in Hz.
    juce::NormalisableRange<float> logFrequencyRange (float minHz, float maxHz)
    {
        return juce::NormalisableRange<float> (
            minHz,
            maxHz,
            [] (float start, float end, float normalised)
            {
                return start * std::pow (end / start, normalised);
            },
            [] (float start, float end, float value)
            {
                return std::log (value / start) / std::log (end / start);
            });
    }
}

SimpleGuitarAudioProcessor::SimpleGuitarAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    outputGainDbParam = apvts.getRawParameterValue (outputGainParamId);
    gateOnParam = apvts.getRawParameterValue (gateOnParamId);
    gateThresholdDbParam = apvts.getRawParameterValue (gateThresholdDbParamId);
    ampInputDbParam = apvts.getRawParameterValue (ampInputDbParamId);
    ampBassDbParam = apvts.getRawParameterValue (ampBassDbParamId);
    ampMidDbParam = apvts.getRawParameterValue (ampMidDbParamId);
    ampTrebleDbParam = apvts.getRawParameterValue (ampTrebleDbParamId);
    ampPresenceDbParam = apvts.getRawParameterValue (ampPresenceDbParamId);
    namNormalizeParam = apvts.getRawParameterValue (namNormalizeParamId);
    cabOnParam = apvts.getRawParameterValue (cabOnParamId);
    cabLowCutHzParam = apvts.getRawParameterValue (cabLowCutHzParamId);
    cabHighCutHzParam = apvts.getRawParameterValue (cabHighCutHzParamId);

    // Message-thread work: make sure the managed library folders exist from
    // the moment the plugin is instantiated, so the first rigState the UI
    // asks for already has somewhere to scan.
    sg::ensureLibraryFoldersExist();
}

SimpleGuitarAudioProcessor::~SimpleGuitarAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout SimpleGuitarAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { outputGainParamId, 1 },
        "Output Gain",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { gateOnParamId, 1 },
        "Gate",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gateThresholdDbParamId, 1 },
        "Gate Threshold",
        juce::NormalisableRange<float> (-90.0f, -20.0f, 0.01f),
        -60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampInputDbParamId, 1 },
        "Input Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampBassDbParamId, 1 },
        "Bass",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampMidDbParamId, 1 },
        "Mid",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampTrebleDbParamId, 1 },
        "Treble",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ampPresenceDbParamId, 1 },
        "Presence",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { namNormalizeParamId, 1 },
        "Normalize",
        true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { cabOnParamId, 1 },
        "Cab",
        true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { cabLowCutHzParamId, 1 },
        "Low Cut",
        logFrequencyRange (20.0f, 400.0f),
        60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { cabHighCutHzParamId, 1 },
        "High Cut",
        logFrequencyRange (2000.0f, 20000.0f),
        8000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    return { params.begin(), params.end() };
}

void SimpleGuitarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numChannels = getTotalNumOutputChannels();

    inputTrimGain.prepare (sampleRate, samplesPerBlock, numChannels);
    gate.prepare (sampleRate, samplesPerBlock, numChannels);
    nam.prepare (sampleRate, samplesPerBlock);
    postEq.prepare (sampleRate, samplesPerBlock, numChannels);
    cab.prepare (sampleRate, samplesPerBlock, numChannels);
    outputGainStage.prepare (sampleRate, samplesPerBlock, numChannels);
    meterTap.reset();
}

void SimpleGuitarAudioProcessor::releaseResources()
{
}

bool SimpleGuitarAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SimpleGuitarAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // All engine setters below are atomic loads only -- no lock, no
    // allocation, no APVTS access on the audio thread.

    // 1. Input trim.
    inputTrimGain.setGainDecibels (ampInputDbParam->load (std::memory_order_relaxed));
    {
        juce::dsp::AudioBlock<float> block (buffer);
        inputTrimGain.process (block);
    }

    // 2. Noise gate.
    gate.setEnabled (gateOnParam->load (std::memory_order_relaxed) >= 0.5f);
    gate.setThresholdDb (gateThresholdDbParam->load (std::memory_order_relaxed));
    gate.process (buffer);

    // 3. NAM amp.
    nam.setNormalize (namNormalizeParam->load (std::memory_order_relaxed) >= 0.5f);
    nam.process (buffer);

    // 4. Post-EQ (Bass/Mid/Treble/Presence, integrated into the amp faceplate).
    postEq.setBassDb (ampBassDbParam->load (std::memory_order_relaxed));
    postEq.setMidDb (ampMidDbParam->load (std::memory_order_relaxed));
    postEq.setTrebleDb (ampTrebleDbParam->load (std::memory_order_relaxed));
    postEq.setPresenceDb (ampPresenceDbParam->load (std::memory_order_relaxed));
    postEq.process (buffer);

    // 5. Cab IR.
    cab.setEnabled (cabOnParam->load (std::memory_order_relaxed) >= 0.5f);
    cab.setLowCutHz (cabLowCutHzParam->load (std::memory_order_relaxed));
    cab.setHighCutHz (cabHighCutHzParam->load (std::memory_order_relaxed));
    cab.process (buffer);

    // 6. Output gain.
    outputGainStage.setGainDecibels (outputGainDbParam->load (std::memory_order_relaxed));
    {
        juce::dsp::AudioBlock<float> block (buffer);
        outputGainStage.process (block);
    }

    // 7. Meter tap (post-chain).
    meterTap.pushBlock (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* SimpleGuitarAudioProcessor::createEditor()
{
    return new SimpleGuitarAudioProcessorEditor (*this);
}

bool SimpleGuitarAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String SimpleGuitarAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleGuitarAudioProcessor::acceptsMidi() const
{
    return false;
}

bool SimpleGuitarAudioProcessor::producesMidi() const
{
    return false;
}

bool SimpleGuitarAudioProcessor::isMidiEffect() const
{
    return false;
}

double SimpleGuitarAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleGuitarAudioProcessor::getNumPrograms()
{
    return 1;
}

int SimpleGuitarAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleGuitarAudioProcessor::setCurrentProgram (int)
{
}

const juce::String SimpleGuitarAudioProcessor::getProgramName (int)
{
    return {};
}

void SimpleGuitarAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void SimpleGuitarAudioProcessor::requestLoadNamModel (const juce::String& path, std::function<void (bool, juce::String)> onDone)
{
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    nam.requestLoad (path, [safeThis, path, onDone] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            if (ok)
            {
                self->currentNamModelPath = path;
                self->apvts.state.setProperty (namModelPathPropertyId, path, nullptr);
            }
        }

        if (onDone)
            onDone (ok, errorOrName);
    });
}

void SimpleGuitarAudioProcessor::requestLoadIr (const juce::String& path, std::function<void (bool, juce::String)> onDone)
{
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    cab.requestLoad (path, [safeThis, path, onDone] (bool ok, juce::String errorOrName)
    {
        if (auto* self = safeThis.get())
        {
            if (ok)
            {
                self->currentIrPath = path;
                self->apvts.state.setProperty (irPathPropertyId, path, nullptr);
            }
        }

        if (onDone)
            onDone (ok, errorOrName);
    });
}

void SimpleGuitarAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SimpleGuitarAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
    else
        return;

    // The host can call setStateInformation from threads other than the
    // message thread (e.g. during project load); requestLoad on the engine
    // blocks must happen on the message thread, so marshal it there. Guard
    // against the processor being destroyed before the async call runs with
    // a weak reference.
    juce::WeakReference<SimpleGuitarAudioProcessor> safeThis (this);

    juce::MessageManager::callAsync ([safeThis]
    {
        if (auto* self = safeThis.get())
            self->restoreLoadedPathsFromState();
    });
}

void SimpleGuitarAudioProcessor::restoreLoadedPathsFromState()
{
    const auto namPath = apvts.state.getProperty (namModelPathPropertyId, "").toString();
    const auto irPath = apvts.state.getProperty (irPathPropertyId, "").toString();

    if (namPath.isNotEmpty() && namPath != currentNamModelPath)
        requestLoadNamModel (namPath, nullptr);

    if (irPath.isNotEmpty() && irPath != currentIrPath)
        requestLoadIr (irPath, nullptr);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleGuitarAudioProcessor();
}
