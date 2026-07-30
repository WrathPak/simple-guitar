#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Smoothed gain processor, parameterised in decibels.

    setGainDecibels() is safe to call from any thread (message thread, parameter
    listener, etc.) — it only performs an atomic store. process() must be called
    from the audio thread; it reads the atomic target and ramps a linear gain
    towards it over a fixed ~20ms smoothing time, so parameter changes never
    produce audible zipper noise or discontinuities.

    Realtime-safety: process() and setGainDecibels() never lock or allocate.
    prepare() may allocate and must be called from a non-audio thread before
    processing starts (or whenever the sample rate/block size changes).
*/
class Gain
{
public:
    Gain() = default;

    /** Prepares the processor for a given sample rate. Call before processing,
        and again whenever the sample rate changes. May allocate; do not call
        from the audio thread. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels) noexcept;

    /** Resets the smoothed gain to its current target with no ramp. */
    void reset() noexcept;

    /** Sets the target gain in decibels. Thread-safe (atomic store); the audio
        thread ramps towards this value over the smoothing time. */
    void setGainDecibels (float newGainDb) noexcept;

    /** Returns the most recently set target gain in decibels. */
    float getGainDecibels() const noexcept { return targetGainDb.load (std::memory_order_relaxed); }

    /** Processes an audio block in place. Audio-thread only: no locks, no
        allocation. Applies a per-sample linear ramp towards the current
        target gain. */
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    /** The fixed gain-smoothing time, in seconds. */
    static constexpr double smoothingTimeSeconds = 0.02;

private:
    std::atomic<float> targetGainDb { 0.0f };
    juce::LinearSmoothedValue<float> smoothedLinearGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Gain)
};

} // namespace sg
