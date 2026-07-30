#include "sg/Compressor.h"

#include <algorithm>
#include <cmath>

namespace sg
{

Compressor::Compressor() = default;
Compressor::~Compressor() = default;

float Compressor::mapThresholdDb (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return juce::jmap (t, 0.0f, 1.0f, minThresholdDb, maxThresholdDb);
}

float Compressor::mapRatio (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return juce::jmap (t, 0.0f, 1.0f, minRatio, maxRatio);
}

float Compressor::mapAttackMs (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return minAttackMs * std::pow (maxAttackMs / minAttackMs, t);
}

float Compressor::mapReleaseMs (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return minReleaseMs * std::pow (maxReleaseMs / minReleaseMs, t);
}

float Compressor::mapLevelDb (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return juce::jmap (t, 0.0f, 1.0f, minLevelDb, maxLevelDb);
}

void Compressor::applyCompressorParameters() noexcept
{
    compressor.setThreshold (mapThresholdDb (amount01.load (std::memory_order_relaxed)));
    compressor.setRatio (mapRatio (amount01.load (std::memory_order_relaxed)));
    compressor.setAttack (mapAttackMs (attack01.load (std::memory_order_relaxed)));
    compressor.setRelease (mapReleaseMs (release01.load (std::memory_order_relaxed)));
}

void Compressor::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    compressor.prepare (spec);
    applyCompressorParameters();

    smoothedMakeupGain.reset (sampleRate, levelSmoothingTimeSeconds);
    smoothedMakeupGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));

    reset();
}

void Compressor::reset() noexcept
{
    compressor.reset();
    applyCompressorParameters();
    smoothedMakeupGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));
}

void Compressor::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void Compressor::setAmount (float v01) noexcept
{
    amount01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Compressor::setAttack (float v01) noexcept
{
    attack01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Compressor::setRelease (float v01) noexcept
{
    release01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Compressor::setLevel (float v01) noexcept
{
    level01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Compressor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    applyCompressorParameters(); // cheap (no allocation); the compressor smooths its own envelope internally

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        compressor.process (context);
    }

    smoothedMakeupGain.setTargetValue (
        juce::Decibels::decibelsToGain (mapLevelDb (level01.load (std::memory_order_relaxed))));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedMakeupGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

} // namespace sg
