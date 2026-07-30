#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Noise gate for cleaning up single-coil hum / amp hiss ahead of the rest of
    the chain. Wraps juce::dsp::NoiseGate with a fixed ratio/attack/release and
    an atomic, audio-thread-safe threshold and enable switch.

    setThresholdDb() and setEnabled() are safe to call from any thread (they
    only perform an atomic store). process() must be called from the audio
    thread; it applies the most recently set threshold at the start of each
    block and then runs the gate.

    Realtime-safety: process(), setThresholdDb() and setEnabled() never lock
    or allocate. prepare() may allocate and must be called from a non-audio
    thread before processing starts (or whenever the sample rate/block
    size/channel count changes).
*/
class NoiseGate
{
public:
    NoiseGate() = default;

    /** Prepares the processor for a given sample rate, block size and channel
        count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough (the buffer
        is left untouched). */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the internal envelope-follower state. Does not affect the
        threshold or enabled state. */
    void reset() noexcept;

    /** Sets the gate threshold in decibels. Thread-safe (atomic store).
        Clamped to [-90, -20] to match the UI-facing parameter range. */
    void setThresholdDb (float newThresholdDb) noexcept;

    /** Returns the most recently set threshold in decibels. */
    float getThresholdDb() const noexcept { return thresholdDb.load (std::memory_order_relaxed); }

    /** Enables or disables the gate. Thread-safe (atomic store). When
        disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the gate is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Minimum/maximum threshold accepted by setThresholdDb(), in decibels. */
    static constexpr float minThresholdDb = -90.0f;
    static constexpr float maxThresholdDb = -20.0f;

private:
    juce::dsp::NoiseGate<float> gate;

    std::atomic<float> thresholdDb { -60.0f };
    std::atomic<bool> enabled { true };

    // Fixed gate behaviour: a fairly hard knee with a snappy attack and a
    // release slow enough to avoid chopping note decays.
    static constexpr float ratio = 8.0f;
    static constexpr float attackTimeMs = 1.0f;
    static constexpr float releaseTimeMs = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseGate)
};

} // namespace sg
