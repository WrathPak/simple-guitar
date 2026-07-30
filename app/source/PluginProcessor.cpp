#include "PluginProcessor.h"
#include "PluginEditor.h"

SimpleGuitarAudioProcessor::SimpleGuitarAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    outputGainDbParam = apvts.getRawParameterValue (outputGainParamId);
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

    return { params.begin(), params.end() };
}

void SimpleGuitarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    gain.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
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

    // Audio-thread-safe: atomic load only, no lock, no allocation.
    gain.setGainDecibels (outputGainDbParam->load (std::memory_order_relaxed));

    juce::dsp::AudioBlock<float> block (buffer);
    gain.process (block);

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
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleGuitarAudioProcessor();
}
