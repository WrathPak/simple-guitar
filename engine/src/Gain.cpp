#include "sg/Gain.h"

namespace sg
{

void Gain::prepare (double sampleRate, int /*maximumBlockSize*/, int /*numChannels*/) noexcept
{
    smoothedLinearGain.reset (sampleRate, smoothingTimeSeconds);
    smoothedLinearGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (targetGainDb.load (std::memory_order_relaxed)));
}

void Gain::reset() noexcept
{
    smoothedLinearGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (targetGainDb.load (std::memory_order_relaxed)));
}

void Gain::setGainDecibels (float newGainDb) noexcept
{
    targetGainDb.store (newGainDb, std::memory_order_relaxed);
}

void Gain::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    smoothedLinearGain.setTargetValue (
        juce::Decibels::decibelsToGain (targetGainDb.load (std::memory_order_relaxed)));

    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    if (! smoothedLinearGain.isSmoothing())
    {
        block.multiplyBy (smoothedLinearGain.getTargetValue());
        return;
    }

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float g = smoothedLinearGain.getNextValue();

        for (std::size_t ch = 0; ch < numChannels; ++ch)
            block.getChannelPointer (ch)[i] *= g;
    }
}

} // namespace sg
