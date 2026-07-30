#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Stereo delay ("echoes"): a single Lagrange-interpolated delay line shared
    across channels, with a soft-clamped feedback loop that runs through a
    fixed ~6kHz lowpass (so repeats get progressively darker while the first
    echo stays full-band) and an equal-power dry/wet mix.

    setTime()/setFeedback()/setMix()/setEnabled() are safe to call from any
    thread (atomic store only) and take normalized [0, 1] values; the
    mapping to real units happens internally. process() must be called from
    the audio thread: delay time, feedback and mix are all smoothed
    per-sample, so knob sweeps never produce pitch-zipper artifacts or
    discontinuities.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate (it resizes the delay line for the maximum delay
    time) and must be called from a non-audio thread before processing
    starts (or whenever the sample rate/block size/channel count changes).
*/
class StereoDelay
{
public:
    StereoDelay() = default;

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the delay line and feedback filters' internal state, and
        snaps all smoothed parameters to their current targets with no
        ramp. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the delay time, normalized [0, 1]. Thread-safe (atomic store).
        Log-mapped from 60ms to 1200ms, and slewed on the audio thread to
        avoid pitch-zipper artifacts on sweeps. */
    void setTime (float v01) noexcept;

    /** Sets the feedback amount, normalized [0, 1]. Thread-safe (atomic
        store). Maps linearly to a loop gain of 0 to 0.85. */
    void setFeedback (float v01) noexcept;

    /** Sets the dry/wet mix, normalized [0, 1] (0 = dry, 1 = fully wet).
        Thread-safe (atomic store). Equal-power law. */
    void setMix (float v01) noexcept;

    /** Returns the most recently set normalized targets (not the ramping
        values). */
    float getTime() const noexcept { return time01.load (std::memory_order_relaxed); }
    float getFeedback() const noexcept { return feedback01.load (std::memory_order_relaxed); }
    float getMix() const noexcept { return mix01.load (std::memory_order_relaxed); }

    /** Delay time range, in milliseconds; log-mapped from setTime(). */
    static constexpr float minTimeMs = 60.0f;
    static constexpr float maxTimeMs = 1200.0f;

    /** Feedback loop gain range; linear-mapped from setFeedback(). */
    static constexpr float maxFeedbackGain = 0.85f;

    /** Fixed lowpass corner inside the feedback loop, in Hz. */
    static constexpr float feedbackLowpassHz = 6000.0f;

    /** Soft-clamp ceiling applied to the signal re-entering the delay line
        via the feedback path (keeps the loop stable even under transient
        buildup, while staying transparent for normal signal levels). */
    static constexpr float feedbackSoftClampCeiling = 1.2f;

    /** Smoothing time for delay-time slews, in seconds. Long enough to hide
        knob-sweep artifacts, short enough to feel responsive. */
    static constexpr double timeSmoothingTimeSeconds = 0.05;

    /** Smoothing times for feedback and mix changes, in seconds. */
    static constexpr double feedbackSmoothingTimeSeconds = 0.02;
    static constexpr double mixSmoothingTimeSeconds = 0.02;

private:
    static float mapTimeMs (float v01) noexcept;
    static float mapFeedbackGain (float v01) noexcept;
    static float softClamp (float x, float ceiling) noexcept;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 4 };

    std::vector<float> feedbackLowpassState;
    float feedbackLowpassCoeff = 0.0f;

    std::atomic<bool> enabled { true };
    std::atomic<float> time01 { 0.3f };
    std::atomic<float> feedback01 { 0.3f };
    std::atomic<float> mix01 { 0.35f };

    juce::LinearSmoothedValue<float> smoothedDelaySamples { minTimeMs * 0.001f * 44100.0f };
    juce::LinearSmoothedValue<float> smoothedFeedbackGain { 0.0f };
    juce::LinearSmoothedValue<float> smoothedMix { 0.0f };

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoDelay)
};

} // namespace sg
