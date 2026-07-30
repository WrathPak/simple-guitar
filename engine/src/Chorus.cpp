#include "sg/Chorus.h"

#include <algorithm>
#include <cmath>

namespace sg
{

Chorus::Chorus() = default;
Chorus::~Chorus() = default;

float Chorus::mapRateHz (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return minRateHz * std::pow (maxRateHz / minRateHz, t);
}

void Chorus::applyChorusParameters() noexcept
{
    chorus.setRate (mapRateHz (rate01.load (std::memory_order_relaxed)));
    chorus.setDepth (std::clamp (depth01.load (std::memory_order_relaxed), 0.0f, 1.0f));
    chorus.setMix (std::clamp (mix01.load (std::memory_order_relaxed), 0.0f, 1.0f));
    chorus.setCentreDelay (centreDelayMs);
    chorus.setFeedback (feedback);
}

void Chorus::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    chorus.prepare (spec);
    applyChorusParameters();

    reset();
}

void Chorus::reset() noexcept
{
    chorus.reset();
    applyChorusParameters();
}

void Chorus::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void Chorus::setRate (float v01) noexcept
{
    rate01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Chorus::setDepth (float v01) noexcept
{
    depth01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Chorus::setMix (float v01) noexcept
{
    mix01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Chorus::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    applyChorusParameters(); // cheap (no allocation); juce::dsp::Chorus smooths internally

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    chorus.process (context);
}

} // namespace sg
