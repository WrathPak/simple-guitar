#pragma once

#include <array>
#include <atomic>
#include <functional>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Cabinet simulator: convolves the signal with a loaded impulse response,
    bracketed by a pre-convolution low-cut and a post-convolution high-cut
    filter. Built on juce::dsp::Convolution (zero-latency, background IR
    loading).

    requestLoad() must be called from the message thread (it does file I/O
    and creates/replaces the convolution engine's impulse response; JUCE
    performs the actual sample-rate conversion / trimming / normalisation on
    a background thread once this call returns). setEnabled()/setLowCutHz()/
    setHighCutHz() are safe to call from any thread (atomic store only).
    isLoaded() and getIrName() are intended for message/UI-thread polling,
    not the audio thread.

    process() must be called from the audio thread; it applies the latest
    low-cut/high-cut targets each block (ramped, to avoid zipper noise) and
    then runs the convolution. Realtime-safety: process(), setEnabled(),
    setLowCutHz() and setHighCutHz() never lock or allocate. prepare() and
    requestLoad() may allocate/do file I/O and must never be called from the
    audio thread.
*/
class IrLoader
{
public:
    IrLoader() = default;

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a clean passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the filters' and convolution engine's internal state. */
    void reset() noexcept;

    /** Asynchronously (from JUCE's point of view) loads a new impulse
        response from a WAV (or other JUCE-supported audio format) file.
        Message-thread only. Calls onDone with (true, cab name) once the
        load request has been validated and handed to the convolution
        engine, or (false, error message) if the file could not be found or
        read as audio. onDone is invoked synchronously on the calling
        thread before requestLoad() returns. */
    void requestLoad (const juce::String& wavPath,
                       std::function<void (bool ok, juce::String errorOrName)> onDone);

    /** Returns true once a requestLoad() call has successfully handed an IR
        to the convolution engine. Message/UI-thread use. */
    bool isLoaded() const noexcept { return loaded.load (std::memory_order_relaxed); }

    /** Returns the filename stem of the most recently loaded IR, or an
        empty string if none has loaded yet. Message/UI-thread use. */
    juce::String getIrName() const noexcept;

    /** Enables or disables cab simulation. Thread-safe (atomic store). When
        disabled, process() is a clean passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether cab simulation is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the pre-convolution high-pass corner frequency, in Hz. Clamped
        to [20, 400]. Thread-safe (atomic store), smoothed on the audio
        thread. */
    void setLowCutHz (float newLowCutHz) noexcept;

    /** Sets the post-convolution low-pass corner frequency, in Hz. Clamped
        to [2000, 20000]. Thread-safe (atomic store), smoothed on the audio
        thread. */
    void setHighCutHz (float newHighCutHz) noexcept;

    /** Returns the most recently set corner frequencies (the targets, not
        the ramping values). */
    float getLowCutHz() const noexcept { return lowCutHz.load (std::memory_order_relaxed); }
    float getHighCutHz() const noexcept { return highCutHz.load (std::memory_order_relaxed); }

    static constexpr float minLowCutHz = 20.0f;
    static constexpr float maxLowCutHz = 400.0f;
    static constexpr float minHighCutHz = 2000.0f;
    static constexpr float maxHighCutHz = 20000.0f;

    /** Fixed Q for both the low-cut and high-cut filters (Butterworth /
        maximally-flat slope). */
    static constexpr float filterQ = 0.70710678f;

    /** The fixed cutoff-smoothing time, in seconds. */
    static constexpr double smoothingTimeSeconds = 0.05;

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                   juce::dsp::IIR::Coefficients<float>>;

    void snapLowCut() noexcept;
    void snapHighCut() noexcept;
    void advanceLowCut (int numSamples) noexcept;
    void advanceHighCut (int numSamples) noexcept;

    juce::dsp::Convolution convolution;
    Filter lowCutFilter, highCutFilter;

    std::atomic<bool> enabled { true };
    std::atomic<bool> loaded { false };
    std::atomic<float> lowCutHz { minLowCutHz };
    std::atomic<float> highCutHz { maxHighCutHz };

    juce::LinearSmoothedValue<float> smoothedLowCutHz { minLowCutHz };
    juce::LinearSmoothedValue<float> smoothedHighCutHz { maxHighCutHz };

    double currentSampleRate = 44100.0;
    int preparedNumChannels = 1;

    mutable juce::CriticalSection nameLock;
    juce::String irName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrLoader)
};

} // namespace sg
