#include "sg/PostEq.h"

#include <algorithm>

namespace sg
{

std::array<float, 6> PostEq::makeBandCoefficients (BandShape shape, double sampleRate,
                                                    float freqHz, float q, float gainFactor) noexcept
{
    using ArrayCoefficients = juce::dsp::IIR::ArrayCoefficients<float>;

    switch (shape)
    {
        case BandShape::lowShelf:
            return ArrayCoefficients::makeLowShelf (sampleRate, freqHz, q, gainFactor);
        case BandShape::highShelf:
            return ArrayCoefficients::makeHighShelf (sampleRate, freqHz, q, gainFactor);
        case BandShape::peak:
        default:
            return ArrayCoefficients::makePeakFilter (sampleRate, freqHz, q, gainFactor);
    }
}

void PostEq::snapBand (Filter& filter, BandShape shape, float freqHz, float q,
                        std::atomic<float>& targetDb, juce::LinearSmoothedValue<float>& smoothedGain) noexcept
{
    const float gainFactor = juce::Decibels::decibelsToGain (targetDb.load (std::memory_order_relaxed));
    smoothedGain.setCurrentAndTargetValue (gainFactor);
    *filter.state = makeBandCoefficients (shape, currentSampleRate, freqHz, q, gainFactor);
}

void PostEq::advanceBand (Filter& filter, BandShape shape, float freqHz, float q,
                           std::atomic<float>& targetDb, juce::LinearSmoothedValue<float>& smoothedGain,
                           int numSamples) noexcept
{
    const float targetGainFactor = juce::Decibels::decibelsToGain (targetDb.load (std::memory_order_relaxed));
    smoothedGain.setTargetValue (targetGainFactor);
    const float gainFactor = smoothedGain.skip (numSamples);
    *filter.state = makeBandCoefficients (shape, currentSampleRate, freqHz, q, gainFactor);
}

void PostEq::prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = (juce::uint32) numChannels;

    bassFilter.prepare (spec);
    midFilter.prepare (spec);
    trebleFilter.prepare (spec);
    presenceFilter.prepare (spec);

    bassGain.reset (sampleRate, smoothingTimeSeconds);
    midGain.reset (sampleRate, smoothingTimeSeconds);
    trebleGain.reset (sampleRate, smoothingTimeSeconds);
    presenceGain.reset (sampleRate, smoothingTimeSeconds);

    snapBand (bassFilter, BandShape::lowShelf, bassHz, shelfQ, bassDb, bassGain);
    snapBand (midFilter, BandShape::peak, midHz, midQ, midDb, midGain);
    snapBand (trebleFilter, BandShape::highShelf, trebleHz, shelfQ, trebleDb, trebleGain);
    snapBand (presenceFilter, BandShape::highShelf, presenceHz, shelfQ, presenceDb, presenceGain);
}

void PostEq::reset() noexcept
{
    bassFilter.reset();
    midFilter.reset();
    trebleFilter.reset();
    presenceFilter.reset();

    snapBand (bassFilter, BandShape::lowShelf, bassHz, shelfQ, bassDb, bassGain);
    snapBand (midFilter, BandShape::peak, midHz, midQ, midDb, midGain);
    snapBand (trebleFilter, BandShape::highShelf, trebleHz, shelfQ, trebleDb, trebleGain);
    snapBand (presenceFilter, BandShape::highShelf, presenceHz, shelfQ, presenceDb, presenceGain);
}

void PostEq::setBassDb (float newGainDb) noexcept
{
    bassDb.store (std::clamp (newGainDb, minGainDb, maxGainDb), std::memory_order_relaxed);
}

void PostEq::setMidDb (float newGainDb) noexcept
{
    midDb.store (std::clamp (newGainDb, minGainDb, maxGainDb), std::memory_order_relaxed);
}

void PostEq::setTrebleDb (float newGainDb) noexcept
{
    trebleDb.store (std::clamp (newGainDb, minGainDb, maxGainDb), std::memory_order_relaxed);
}

void PostEq::setPresenceDb (float newGainDb) noexcept
{
    presenceDb.store (std::clamp (newGainDb, minGainDb, maxGainDb), std::memory_order_relaxed);
}

void PostEq::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();

    advanceBand (bassFilter, BandShape::lowShelf, bassHz, shelfQ, bassDb, bassGain, numSamples);
    advanceBand (midFilter, BandShape::peak, midHz, midQ, midDb, midGain, numSamples);
    advanceBand (trebleFilter, BandShape::highShelf, trebleHz, shelfQ, trebleDb, trebleGain, numSamples);
    advanceBand (presenceFilter, BandShape::highShelf, presenceHz, shelfQ, presenceDb, presenceGain, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    bassFilter.process (context);
    midFilter.process (context);
    trebleFilter.process (context);
    presenceFilter.process (context);
}

} // namespace sg
