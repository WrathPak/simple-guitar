#include "sg/NoiseGate.h"

#include <algorithm>

namespace sg
{

void NoiseGate::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    gate.setRatio (ratio);
    gate.setAttack (attackTimeMs);
    gate.setRelease (releaseTimeMs);
    gate.setThreshold (thresholdDb.load (std::memory_order_relaxed));

    gate.prepare (spec);
}

void NoiseGate::reset() noexcept
{
    gate.reset();
}

void NoiseGate::setThresholdDb (float newThresholdDb) noexcept
{
    thresholdDb.store (std::clamp (newThresholdDb, minThresholdDb, maxThresholdDb),
                        std::memory_order_relaxed);
}

void NoiseGate::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void NoiseGate::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    // setThreshold() is just cheap arithmetic (no allocation), so it is safe
    // to apply the latest atomic target once per block on the audio thread.
    gate.setThreshold (thresholdDb.load (std::memory_order_relaxed));

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    gate.process (context);
}

} // namespace sg
