#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Tremolo ("shiver"): a hand-rolled per-sample LFO gain modulator (no
    juce::dsp engine underneath -- just a phase accumulator and a waveshaped
    sine). The same LFO gain is applied to every channel (mono modulation,
    the classic tremolo behaviour).

    setRate()/setDepth()/setShape()/setEnabled() are safe to call from any
    thread (atomic store only) and take normalized [0, 1] values; the
    mapping to real units happens internally. process() must be called from
    the audio thread: rate/depth/shape are all smoothed per-sample (the LFO
    phase itself is advanced using the smoothed instantaneous rate, so a
    rate change is a click-free "chirp" rather than a phase jump), so
    parameter moves never produce discontinuities.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate (it resets the smoothers) and must be called from
    a non-audio thread before processing starts (or whenever the sample
    rate/block size changes).
*/
class Tremolo
{
public:
    Tremolo() = default;

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the LFO phase and snaps all smoothed parameters to their
        current targets with no ramp. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the LFO rate, normalized [0, 1]. Thread-safe (atomic store).
        Log-mapped from 0.5Hz to 12Hz. */
    void setRate (float v01) noexcept;

    /** Sets the modulation depth, normalized [0, 1]. Thread-safe (atomic
        store). 0 = no effect (gain pinned at unity), 1 = gain dips all the
        way to silence at the LFO trough. */
    void setDepth (float v01) noexcept;

    /** Sets the LFO shape, normalized [0, 1]: 0 = pure sine, 1 = near-square
        (via waveshaping the sine, not a separate oscillator). Thread-safe
        (atomic store). */
    void setShape (float v01) noexcept;

    /** Returns the most recently set normalized targets. */
    float getRate() const noexcept { return rate01.load (std::memory_order_relaxed); }
    float getDepth() const noexcept { return depth01.load (std::memory_order_relaxed); }
    float getShape() const noexcept { return shape01.load (std::memory_order_relaxed); }

    /** Rate range, in Hz; log-mapped from setRate(). */
    static constexpr float minRateHz = 0.5f;
    static constexpr float maxRateHz = 12.0f;

    /** Waveshaping exponent range applied to the sine LFO (see
        shapeLfo()): 1.0 leaves it a pure sine, values well below 1
        progressively square it off. */
    static constexpr float sineShapeExponent = 1.0f;
    static constexpr float squareShapeExponent = 0.12f;

    /** Smoothing times, in seconds. */
    static constexpr double rateSmoothingTimeSeconds = 0.05;
    static constexpr double depthSmoothingTimeSeconds = 0.02;
    static constexpr double shapeSmoothingTimeSeconds = 0.05;

private:
    static float mapRateHz (float v01) noexcept;

    /** Waveshapes a unit sine sample (`x` in [-1, 1]) towards a square wave
        as `exponent` decreases below 1: sign(x) * |x|^exponent. Pure
        function, used by process(). */
    static float shapeLfo (float x, float exponent) noexcept;

    std::atomic<bool> enabled { true };
    std::atomic<float> rate01 { 0.5f };
    std::atomic<float> depth01 { 0.5f };
    std::atomic<float> shape01 { 0.0f };

    juce::LinearSmoothedValue<float> smoothedRateHz { 2.0f };
    juce::LinearSmoothedValue<float> smoothedDepth { 0.5f };
    juce::LinearSmoothedValue<float> smoothedShapeExponent { sineShapeExponent };

    double phase = 0.0; // 0..1, wraps
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tremolo)
};

} // namespace sg
