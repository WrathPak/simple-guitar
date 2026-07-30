#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
    Pure helpers backing the hosted-plugin pedal slot (see PluginHosting.h
    for the runtime wrapper): the P1 wet/dry blend math and the base64
    encoding used for the per-slot plugin state blob (slot{N}PluginState
    APVTS state-tree property). Factored out of the runtime pieces so
    they're directly Catch2-testable without instantiating a real
    juce::AudioPluginInstance -- same spirit as computeMovePermutation()
    living apart from PedalSlotHost.
*/
namespace sg
{

/** The equal-power wet/dry gain pair for a hosted-plugin slot's P1 mix
    knob. mix01 = 0 is fully dry (plugin inaudible), 1 is fully wet, 0.5
    (the flat slot default) is the equal-power midpoint (both ~0.707); the
    two gains always satisfy wet^2 + dry^2 == 1 so the blend never dips or
    bumps perceived level for correlated signals. Audio-thread safe (pure
    math). */
struct WetDryGains
{
    float wet;
    float dry;
};

inline WetDryGains equalPowerWetDry (float mix01) noexcept
{
    const float clamped = juce::jlimit (0.0f, 1.0f, mix01);
    const float angle = clamped * juce::MathConstants<float>::halfPi;
    return { std::sin (angle), std::cos (angle) };
}

/** Blends `dry` into `wetInOut` in place with the equal-power gains for
    `mix01`: wetInOut = wet(mix)*wetInOut + dry(mix)*dry. Channel/sample
    counts must match (the caller owns that invariant -- both buffers come
    from the same slot's block). Audio-thread safe: no locks, no
    allocation. */
inline void blendWetDryInPlace (juce::AudioBuffer<float>& wetInOut,
                                const juce::AudioBuffer<float>& dry,
                                float mix01) noexcept
{
    const auto gains = equalPowerWetDry (mix01);
    const int numChannels = juce::jmin (wetInOut.getNumChannels(), dry.getNumChannels());
    const int numSamples = juce::jmin (wetInOut.getNumSamples(), dry.getNumSamples());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        wetInOut.applyGain (ch, 0, numSamples, gains.wet);
        wetInOut.addFrom (ch, 0, dry, ch, 0, numSamples, gains.dry);
    }
}

/** Encodes a raw state blob (a hosted instance's getStateInformation()
    output) as base64 text for storage in an APVTS state-tree property.
    Pure; message-thread use. */
juce::String stateBlobToBase64 (const juce::MemoryBlock& blob);

/** Decodes stateBlobToBase64()'s output back into a raw blob. Returns false
    (leaving outBlob untouched) on malformed base64. An empty input decodes
    to an empty blob (returns true). Pure; message-thread use. */
bool base64ToStateBlob (const juce::String& base64Text, juce::MemoryBlock& outBlob);

} // namespace sg
