#include "sg/StereoDelay.h"

#include <algorithm>
#include <cmath>

namespace sg
{

float StereoDelay::mapTimeMs (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    // Log-mapped sweep from 60ms to 1200ms.
    return minTimeMs * std::pow (maxTimeMs / minTimeMs, t);
}

float StereoDelay::mapFeedbackGain (float v01) noexcept
{
    return std::clamp (v01, 0.0f, 1.0f) * maxFeedbackGain;
}

float StereoDelay::softClamp (float x, float ceiling) noexcept
{
    // Near-linear for |x| well inside the ceiling; saturates smoothly
    // beyond it so the feedback loop can never run away.
    return ceiling * std::tanh (x / ceiling);
}

void StereoDelay::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    currentSampleRate = sampleRate;

    const int maxDelaySamples = (int) std::ceil (sampleRate * (maxTimeMs * 0.001)) + 8;
    delayLine.setMaximumDelayInSamples (maxDelaySamples);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;
    delayLine.prepare (spec);

    feedbackLowpassState.assign ((size_t) numChannels, 0.0f);
    feedbackLowpassCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                             * feedbackLowpassHz / (float) sampleRate);

    smoothedDelaySamples.reset (sampleRate, timeSmoothingTimeSeconds);
    smoothedFeedbackGain.reset (sampleRate, feedbackSmoothingTimeSeconds);
    smoothedMix.reset (sampleRate, mixSmoothingTimeSeconds);

    const float delaySamplesTarget = mapTimeMs (time01.load (std::memory_order_relaxed)) * 0.001f * (float) sampleRate;
    smoothedDelaySamples.setCurrentAndTargetValue (delaySamplesTarget);
    smoothedFeedbackGain.setCurrentAndTargetValue (mapFeedbackGain (feedback01.load (std::memory_order_relaxed)));
    smoothedMix.setCurrentAndTargetValue (mix01.load (std::memory_order_relaxed));

    reset();
}

void StereoDelay::reset() noexcept
{
    delayLine.reset();
    std::fill (feedbackLowpassState.begin(), feedbackLowpassState.end(), 0.0f);

    const float delaySamplesTarget = mapTimeMs (time01.load (std::memory_order_relaxed)) * 0.001f * (float) currentSampleRate;
    smoothedDelaySamples.setCurrentAndTargetValue (delaySamplesTarget);
    smoothedFeedbackGain.setCurrentAndTargetValue (mapFeedbackGain (feedback01.load (std::memory_order_relaxed)));
    smoothedMix.setCurrentAndTargetValue (mix01.load (std::memory_order_relaxed));
}

void StereoDelay::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void StereoDelay::setTime (float v01) noexcept
{
    time01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void StereoDelay::setFeedback (float v01) noexcept
{
    feedback01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void StereoDelay::setMix (float v01) noexcept
{
    mix01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void StereoDelay::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const float delaySamplesTarget = mapTimeMs (time01.load (std::memory_order_relaxed)) * 0.001f * (float) currentSampleRate;
    smoothedDelaySamples.setTargetValue (delaySamplesTarget);
    smoothedFeedbackGain.setTargetValue (mapFeedbackGain (feedback01.load (std::memory_order_relaxed)));
    smoothedMix.setTargetValue (mix01.load (std::memory_order_relaxed));

    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const float delaySamples = smoothedDelaySamples.getNextValue();
        const float fbGain = smoothedFeedbackGain.getNextValue();
        const float mix = smoothedMix.getNextValue();

        const float wetGain = std::sin (mix * halfPi);
        const float dryGain = std::cos (mix * halfPi);

        delayLine.setDelay (delaySamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float inSample = buffer.getSample (ch, i);

            const float delayed = delayLine.popSample (ch);

            // One-pole lowpass inside the feedback path only -- repeats get
            // progressively darker, the first echo (tapped above) stays
            // full-band.
            float& lpState = feedbackLowpassState[(size_t) ch];
            lpState += feedbackLowpassCoeff * (delayed - lpState);

            const float feedbackSample = softClamp (lpState * fbGain, feedbackSoftClampCeiling);
            delayLine.pushSample (ch, inSample + feedbackSample);

            buffer.setSample (ch, i, dryGain * inSample + wetGain * delayed);
        }
    }
}

} // namespace sg
