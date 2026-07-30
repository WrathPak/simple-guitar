#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Downward compressor ("squeeze"): juce::dsp::Compressor (feed-forward,
    RMS-ish envelope) driven by a single "amount" control that moves
    threshold and ratio together, plus independent attack/release, and a
    smoothed makeup-gain stage on top (the compressor itself does not apply
    makeup gain).

    setAmount()/setAttack()/setRelease()/setLevel()/setEnabled() are safe to
    call from any thread (atomic store only) and take normalized [0, 1]
    values; the mapping to real units happens internally. process() must be
    called from the audio thread: threshold/ratio/attack/release are applied
    once per block (juce::dsp::Compressor smooths its own envelope
    internally, same convention as PostEq/IrLoader's per-block coefficient
    recomputation elsewhere in this engine); the makeup gain is smoothed
    per-sample on top so level changes never zipper.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate and must be called from a non-audio thread before
    processing starts (or whenever the sample rate/block size/channel count
    changes).
*/
class Compressor
{
public:
    Compressor();
    ~Compressor();

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the compressor's envelope state and snaps the smoothed makeup
        gain to its current target with no ramp. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the compression amount, normalized [0, 1]. Thread-safe (atomic
        store). Moves threshold and ratio together: 0 is barely-there
        compression (threshold -6dB, ratio 2:1), 1 is heavy squeeze
        (threshold -40dB, ratio 8:1). */
    void setAmount (float v01) noexcept;

    /** Sets the attack time, normalized [0, 1]. Thread-safe (atomic store).
        Log-mapped from 1ms to 50ms. */
    void setAttack (float v01) noexcept;

    /** Sets the release time, normalized [0, 1]. Thread-safe (atomic store).
        Log-mapped from 40ms to 400ms. */
    void setRelease (float v01) noexcept;

    /** Sets the output (makeup) level, normalized [0, 1]. Thread-safe
        (atomic store). Linear-mapped from -12dB to +12dB; 0.5 is unity. */
    void setLevel (float v01) noexcept;

    /** Returns the most recently set normalized targets (not the ramping
        values). */
    float getAmount() const noexcept { return amount01.load (std::memory_order_relaxed); }
    float getAttack() const noexcept { return attack01.load (std::memory_order_relaxed); }
    float getRelease() const noexcept { return release01.load (std::memory_order_relaxed); }
    float getLevel() const noexcept { return level01.load (std::memory_order_relaxed); }

    /** Amount maps threshold and ratio together across these ranges. */
    static constexpr float minThresholdDb = -6.0f;
    static constexpr float maxThresholdDb = -40.0f;
    static constexpr float minRatio = 2.0f;
    static constexpr float maxRatio = 8.0f;

    /** Attack/release ranges, in milliseconds; log-mapped. */
    static constexpr float minAttackMs = 1.0f;
    static constexpr float maxAttackMs = 50.0f;
    static constexpr float minReleaseMs = 40.0f;
    static constexpr float maxReleaseMs = 400.0f;

    /** Level (makeup gain) range, in decibels; linear-mapped, 0.5 = unity. */
    static constexpr float minLevelDb = -12.0f;
    static constexpr float maxLevelDb = 12.0f;

    /** Smoothing time for the makeup-gain stage, in seconds. */
    static constexpr double levelSmoothingTimeSeconds = 0.02;

private:
    static float mapThresholdDb (float v01) noexcept;
    static float mapRatio (float v01) noexcept;
    static float mapAttackMs (float v01) noexcept;
    static float mapReleaseMs (float v01) noexcept;
    static float mapLevelDb (float v01) noexcept;

    void applyCompressorParameters() noexcept;

    juce::dsp::Compressor<float> compressor;

    std::atomic<bool> enabled { true };
    std::atomic<float> amount01 { 0.5f };
    std::atomic<float> attack01 { 0.5f };
    std::atomic<float> release01 { 0.5f };
    std::atomic<float> level01 { 0.5f };

    juce::LinearSmoothedValue<float> smoothedMakeupGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Compressor)
};

} // namespace sg
