#include "sg/Tremolo.h"

#include <algorithm>
#include <cmath>

namespace sg
{

float Tremolo::mapRateHz (float v01) noexcept
{
    const float t = std::clamp (v01, 0.0f, 1.0f);
    return minRateHz * std::pow (maxRateHz / minRateHz, t);
}

float Tremolo::shapeLfo (float x, float exponent) noexcept
{
    const float sign = x < 0.0f ? -1.0f : 1.0f;
    return sign * std::pow (std::abs (x), exponent);
}

void Tremolo::prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) noexcept
{
    currentSampleRate = sampleRate;

    smoothedRateHz.reset (sampleRate, rateSmoothingTimeSeconds);
    smoothedDepth.reset (sampleRate, depthSmoothingTimeSeconds);
    smoothedShapeExponent.reset (sampleRate, shapeSmoothingTimeSeconds);

    smoothedRateHz.setCurrentAndTargetValue (mapRateHz (rate01.load (std::memory_order_relaxed)));
    smoothedDepth.setCurrentAndTargetValue (std::clamp (depth01.load (std::memory_order_relaxed), 0.0f, 1.0f));
    smoothedShapeExponent.setCurrentAndTargetValue (
        juce::jmap (std::clamp (shape01.load (std::memory_order_relaxed), 0.0f, 1.0f),
                    0.0f, 1.0f, sineShapeExponent, squareShapeExponent));

    reset();
}

void Tremolo::reset() noexcept
{
    phase = 0.0;

    smoothedRateHz.setCurrentAndTargetValue (mapRateHz (rate01.load (std::memory_order_relaxed)));
    smoothedDepth.setCurrentAndTargetValue (std::clamp (depth01.load (std::memory_order_relaxed), 0.0f, 1.0f));
    smoothedShapeExponent.setCurrentAndTargetValue (
        juce::jmap (std::clamp (shape01.load (std::memory_order_relaxed), 0.0f, 1.0f),
                    0.0f, 1.0f, sineShapeExponent, squareShapeExponent));
}

void Tremolo::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

void Tremolo::setRate (float v01) noexcept
{
    rate01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Tremolo::setDepth (float v01) noexcept
{
    depth01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Tremolo::setShape (float v01) noexcept
{
    shape01.store (std::clamp (v01, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Tremolo::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return; // bit-exact passthrough

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    smoothedRateHz.setTargetValue (mapRateHz (rate01.load (std::memory_order_relaxed)));
    smoothedDepth.setTargetValue (std::clamp (depth01.load (std::memory_order_relaxed), 0.0f, 1.0f));
    smoothedShapeExponent.setTargetValue (
        juce::jmap (std::clamp (shape01.load (std::memory_order_relaxed), 0.0f, 1.0f),
                    0.0f, 1.0f, sineShapeExponent, squareShapeExponent));

    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const double rateHz = (double) smoothedRateHz.getNextValue();
        const float depth = smoothedDepth.getNextValue();
        const float exponent = smoothedShapeExponent.getNextValue();

        phase += rateHz / currentSampleRate;
        if (phase >= 1.0)
            phase -= std::floor (phase);

        const float rawLfo = (float) std::sin (twoPi * phase);
        const float shaped = shapeLfo (rawLfo, exponent);
        const float modFactor = 0.5f * (shaped + 1.0f); // 0..1
        const float gain = 1.0f - depth * (1.0f - modFactor);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
    }
}

} // namespace sg
