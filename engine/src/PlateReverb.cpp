#include "sg/PlateReverb.h"

#include <algorithm>
#include <cmath>

namespace sg
{

void PlateReverb::applyReverbParameters() noexcept
{
    const float decay = std::clamp (decay01.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const float tone = std::clamp (tone01.load (std::memory_order_relaxed), 0.0f, 1.0f);

    juce::dsp::Reverb::Parameters params;
    params.roomSize = juce::jmap (decay, 0.0f, 1.0f, minRoomSize, maxRoomSize);
    params.width = juce::jmap (decay, 0.0f, 1.0f, minWidth, maxWidth);
    params.damping = juce::jmap (tone, 0.0f, 1.0f, maxDamping, minDamping); // inverse: bright = low damping
    params.wetLevel = 1.0f; // dry/wet handled entirely outside, for bit-exact mix=0
    params.dryLevel = 0.0f;
    params.freezeMode = 0.0f;

    reverb.setParameters (params);
}

void PlateReverb::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    reverb.prepare (spec);
    applyReverbParameters();

    dryScratch.setSize (numChannels, maxBlockSize);

    smoothedMix.reset (sampleRate, mixSmoothingTimeSeconds);
    smoothedMix.setCurrentAndTargetValue (mix01.load (std::memory_order_relaxed));

    reset();
}

void PlateReverb::reset() noexcept
{
    reverb.reset();
    applyReverbParameters();
    smoothedMix.setCurrentAndTargetValue (mix01.load (std::memory_order_relaxed));
}

void PlateReverb::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void PlateReverb::setDecay (float v01) noexcept
{
    decay01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void PlateReverb::setTone (float v01) noexcept
{
    tone01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void PlateReverb::setMix (float v01) noexcept
{
    mix01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void PlateReverb::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    smoothedMix.setTargetValue (mix01.load (std::memory_order_relaxed));

    if (! smoothedMix.isSmoothing() && smoothedMix.getCurrentValue() <= 0.0f)
        return; // bit-exact dry passthrough -- buffer is left completely untouched

    applyReverbParameters(); // cheap (no allocation); juce::dsp::Reverb smooths internally

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    dryScratch.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context); // buffer now holds ~100% wet reverb output

    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = smoothedMix.getNextValue();
        const float wetGain = std::sin (mix * halfPi);
        const float dryGain = std::cos (mix * halfPi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = dryScratch.getSample (ch, i);
            const float wet = buffer.getSample (ch, i);
            buffer.setSample (ch, i, dryGain * dry + wetGain * wet);
        }
    }
}

} // namespace sg
