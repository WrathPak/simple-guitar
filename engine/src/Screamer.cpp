#include "sg/Screamer.h"

#include <algorithm>
#include <cmath>

namespace sg
{

Screamer::Screamer() = default;
Screamer::~Screamer() = default;

float Screamer::mapDriveGain (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    // Exponential (perceptually even) sweep from 1x to 40x.
    return minDriveGain * std::pow (maxDriveGain / minDriveGain, t);
}

float Screamer::mapToneHz (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    // Log-mapped sweep from 1kHz (dark) to 8kHz (bright).
    return minToneHz * std::pow (maxToneHz / minToneHz, t);
}

float Screamer::mapLevelDb (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);

    if (t <= unityLevelV01)
        return juce::jmap (t, 0.0f, unityLevelV01, minLevelDb, unityLevelDb);

    return juce::jmap (t, unityLevelV01, 1.0f, unityLevelDb, maxLevelDb);
}

void Screamer::snapToneFilter() noexcept
{
    const float hz = smoothedToneHz.getCurrentValue();
    *toneFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (currentSampleRate, hz, preFilterQ);
}

void Screamer::advanceToneFilter (int numSamples) noexcept
{
    const float hz = smoothedToneHz.skip (numSamples);
    *toneFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (currentSampleRate, hz, preFilterQ);
}

void Screamer::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    preHighpassFilter.prepare (spec);
    preLowpassFilter.prepare (spec);
    toneFilter.prepare (spec);

    *preHighpassFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        sampleRate, preHighpassHz, preFilterQ);
    *preLowpassFilter.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
        sampleRate, preHighpassHz, preFilterQ);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels,
        (size_t) oversamplingFactorLog2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,  // isMaxQuality
        true); // useIntegerLatency -- keeps getLatencySamples() a clean integer
    oversampler->initProcessing ((size_t) maxBlockSize);

    smoothedDriveGain.reset (sampleRate, driveSmoothingTimeSeconds);
    smoothedToneHz.reset (sampleRate, toneSmoothingTimeSeconds);
    smoothedOutputGain.reset (sampleRate, levelSmoothingTimeSeconds);

    smoothedDriveGain.setCurrentAndTargetValue (mapDriveGain (drive01.load (std::memory_order_relaxed)));
    smoothedToneHz.setCurrentAndTargetValue (mapToneHz (tone01.load (std::memory_order_relaxed)));
    smoothedOutputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));

    snapToneFilter();

    highpassScratch.setSize (numChannels, maxBlockSize);
    lowpassScratch.setSize (numChannels, maxBlockSize);
    driveGainPerSample.assign ((size_t) maxBlockSize, minDriveGain);

    reset();
}

void Screamer::reset() noexcept
{
    preHighpassFilter.reset();
    preLowpassFilter.reset();
    toneFilter.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    smoothedDriveGain.setCurrentAndTargetValue (mapDriveGain (drive01.load (std::memory_order_relaxed)));
    smoothedToneHz.setCurrentAndTargetValue (mapToneHz (tone01.load (std::memory_order_relaxed)));
    smoothedOutputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));

    snapToneFilter();
}

void Screamer::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void Screamer::setDrive (float v01) noexcept
{
    drive01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Screamer::setTone (float v01) noexcept
{
    tone01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Screamer::setLevel (float v01) noexcept
{
    level01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

int Screamer::getLatencySamples() const noexcept
{
    if (oversampler == nullptr)
        return 0;

    return (int) std::lround (oversampler->getLatencyInSamples());
}

void Screamer::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    jassert (numSamples <= (int) driveGainPerSample.size());

    // --- Fixed pre-clip mid-focused shaping: highpass into the clipper,
    //     with a touch of the dry low end blended back in for punch. ---
    highpassScratch.setSize (numChannels, numSamples, false, false, true);
    lowpassScratch.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        highpassScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        lowpassScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    {
        juce::dsp::AudioBlock<float> hpBlock (highpassScratch);
        juce::dsp::ProcessContextReplacing<float> hpContext (hpBlock);
        preHighpassFilter.process (hpContext);

        juce::dsp::AudioBlock<float> lpBlock (lowpassScratch);
        juce::dsp::ProcessContextReplacing<float> lpContext (lpBlock);
        preLowpassFilter.process (lpContext);
    }

    smoothedDriveGain.setTargetValue (mapDriveGain (drive01.load (std::memory_order_relaxed)));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedDriveGain.getNextValue();
        driveGainPerSample[(size_t) i] = g;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float shaped = highpassScratch.getSample (ch, i) + dryLowBlend * lowpassScratch.getSample (ch, i);
            buffer.setSample (ch, i, shaped * g);
        }
    }

    // --- Soft clip at 2x oversampling to control aliasing. ---
    juce::dsp::AudioBlock<float> block (buffer);
    auto oversampledBlock = oversampler->processSamplesUp (juce::dsp::AudioBlock<const float> (block));

    const auto numOversampledSamples = oversampledBlock.getNumSamples();
    for (size_t ch = 0; ch < oversampledBlock.getNumChannels(); ++ch)
    {
        float* data = oversampledBlock.getChannelPointer (ch);
        for (size_t i = 0; i < numOversampledSamples; ++i)
            data[i] = std::tanh (data[i]);
    }

    oversampler->processSamplesDown (block);

    // --- Auto makeup gain (keeps perceived loudness roughly steady across
    //     the drive range) + post-clip tone tilt + output level. ---
    advanceToneFilter (numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float makeup = 1.0f / std::tanh (driveGainPerSample[(size_t) i]);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * makeup);
    }

    juce::dsp::ProcessContextReplacing<float> toneContext (block);
    toneFilter.process (toneContext);

    smoothedOutputGain.setTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedOutputGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

} // namespace sg
