#include "sg/IrLoader.h"

#include <algorithm>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

namespace sg
{

void IrLoader::snapLowCut() noexcept
{
    const float target = lowCutHz.load (std::memory_order_relaxed);
    smoothedLowCutHz.setCurrentAndTargetValue (target);
    *lowCutFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        currentSampleRate, target, filterQ);
}

void IrLoader::snapHighCut() noexcept
{
    const float target = highCutHz.load (std::memory_order_relaxed);
    smoothedHighCutHz.setCurrentAndTargetValue (target);
    *highCutFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
        currentSampleRate, target, filterQ);
}

void IrLoader::advanceLowCut (int numSamples) noexcept
{
    const float target = lowCutHz.load (std::memory_order_relaxed);
    smoothedLowCutHz.setTargetValue (target);
    const float freq = smoothedLowCutHz.skip (numSamples);
    *lowCutFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        currentSampleRate, freq, filterQ);
}

void IrLoader::advanceHighCut (int numSamples) noexcept
{
    const float target = highCutHz.load (std::memory_order_relaxed);
    smoothedHighCutHz.setTargetValue (target);
    const float freq = smoothedHighCutHz.skip (numSamples);
    *highCutFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
        currentSampleRate, freq, filterQ);
}

void IrLoader::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    currentSampleRate = sampleRate;
    preparedNumChannels = numChannels;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    convolution.prepare (spec);

    lowCutFilter.prepare (spec);
    highCutFilter.prepare (spec);

    smoothedLowCutHz.reset (sampleRate, smoothingTimeSeconds);
    smoothedHighCutHz.reset (sampleRate, smoothingTimeSeconds);

    snapLowCut();
    snapHighCut();
}

void IrLoader::reset() noexcept
{
    convolution.reset();
    lowCutFilter.reset();
    highCutFilter.reset();

    snapLowCut();
    snapHighCut();
}

void IrLoader::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void IrLoader::setLowCutHz (float newLowCutHz) noexcept
{
    lowCutHz.store (std::clamp (newLowCutHz, minLowCutHz, maxLowCutHz), std::memory_order_relaxed);
}

void IrLoader::setHighCutHz (float newHighCutHz) noexcept
{
    highCutHz.store (std::clamp (newHighCutHz, minHighCutHz, maxHighCutHz), std::memory_order_relaxed);
}

juce::String IrLoader::getIrName() const noexcept
{
    const juce::ScopedLock sl (nameLock);
    return irName;
}

void IrLoader::requestLoad (const juce::String& wavPath,
                             std::function<void (bool ok, juce::String errorOrName)> onDone)
{
    const juce::File file (wavPath);

    if (! file.existsAsFile())
    {
        if (onDone)
            onDone (false, "IR file not found: " + wavPath);
        return;
    }

    // Validate the file is actually readable as audio before handing it to
    // Convolution, so callers get a meaningful error instead of silence.
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

        if (reader == nullptr)
        {
            if (onDone)
                onDone (false, "Unsupported or unreadable audio file: " + file.getFileName());
            return;
        }
    } // reader closed before Convolution reopens the file internally

    const auto stem = file.getFileNameWithoutExtension();

    convolution.loadImpulseResponse (
        file,
        preparedNumChannels > 1 ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::yes,
        0,
        juce::dsp::Convolution::Normalise::yes);

    {
        const juce::ScopedLock sl (nameLock);
        irName = stem;
    }
    loaded.store (true, std::memory_order_relaxed);

    if (onDone)
        onDone (true, stem);
}

void IrLoader::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // clean passthrough

    const int numSamples = buffer.getNumSamples();

    advanceLowCut (numSamples);
    advanceHighCut (numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    lowCutFilter.process (context);
    convolution.process (context);
    highCutFilter.process (context);
}

} // namespace sg
