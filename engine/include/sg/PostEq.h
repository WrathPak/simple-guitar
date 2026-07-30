#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Fixed 4-band amp-voicing EQ: bass (low shelf), mid (peak), treble (high
    shelf) and presence (high shelf), built on juce::dsp::IIR biquads.

    setBassDb()/setMidDb()/setTrebleDb()/setPresenceDb() are safe to call from
    any thread (atomic store only). process() must be called from the audio
    thread; each block it ramps every band's gain towards its latest target
    over a fixed smoothing time and rebuilds that band's biquad coefficients
    in place (no heap allocation once prepare() has run), so parameter moves
    never produce zipper noise or coefficient discontinuities.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate and must be called from a non-audio thread before
    processing starts (or whenever the sample rate/block size/channel count
    changes).
*/
class PostEq
{
public:
    PostEq() = default;

    /** Prepares the processor for a given sample rate, block size and channel
        count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the filters' internal state (e.g. after a transport jump).
        Re-snaps coefficients to the current targets with no ramp. */
    void reset() noexcept;

    /** Sets the low-shelf gain (~100 Hz), in dB. Thread-safe. Clamped to
        [-12, +12] and smoothed on the audio thread. */
    void setBassDb (float newGainDb) noexcept;

    /** Sets the mid peak gain (~750 Hz, Q ~0.8), in dB. Thread-safe. Clamped
        to [-12, +12] and smoothed on the audio thread. */
    void setMidDb (float newGainDb) noexcept;

    /** Sets the high-shelf gain (~3 kHz), in dB. Thread-safe. Clamped to
        [-12, +12] and smoothed on the audio thread. */
    void setTrebleDb (float newGainDb) noexcept;

    /** Sets the high-shelf gain (~6.5 kHz), in dB. Thread-safe. Clamped to
        [-12, +12] and smoothed on the audio thread. */
    void setPresenceDb (float newGainDb) noexcept;

    /** Returns the most recently set target gains, in dB (not the ramping
        value). */
    float getBassDb() const noexcept { return bassDb.load (std::memory_order_relaxed); }
    float getMidDb() const noexcept { return midDb.load (std::memory_order_relaxed); }
    float getTrebleDb() const noexcept { return trebleDb.load (std::memory_order_relaxed); }
    float getPresenceDb() const noexcept { return presenceDb.load (std::memory_order_relaxed); }

    /** Gain range accepted by all four setters, in dB. */
    static constexpr float minGainDb = -12.0f;
    static constexpr float maxGainDb = 12.0f;

    /** Fixed band centre/corner frequencies and Qs, in Hz / dimensionless. */
    static constexpr float bassHz = 100.0f;
    static constexpr float midHz = 750.0f;
    static constexpr float midQ = 0.8f;
    static constexpr float trebleHz = 3000.0f;
    static constexpr float presenceHz = 6500.0f;
    static constexpr float shelfQ = 0.70710678f; // Butterworth (maximally flat) shelf slope

    /** The fixed gain-smoothing time, in seconds. */
    static constexpr double smoothingTimeSeconds = 0.05;

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                   juce::dsp::IIR::Coefficients<float>>;

    enum class BandShape { lowShelf, peak, highShelf };

    // Computes biquad coefficients for one band shape. Uses the
    // non-allocating juce::dsp::IIR::ArrayCoefficients formulas so this can
    // safely run on the audio thread every block.
    static std::array<float, 6> makeBandCoefficients (BandShape shape, double sampleRate,
                                                       float freqHz, float q, float gainFactor) noexcept;

    // Rebuilds a band's coefficients in place from its current target dB,
    // with no ramp (used by prepare()/reset()).
    void snapBand (Filter& filter, BandShape shape, float freqHz, float q,
                    std::atomic<float>& targetDb, juce::LinearSmoothedValue<float>& smoothedGain) noexcept;

    // Advances a band's smoothed gain by numSamples and rebuilds its
    // coefficients in place from the ramped value (used by process()).
    void advanceBand (Filter& filter, BandShape shape, float freqHz, float q,
                       std::atomic<float>& targetDb, juce::LinearSmoothedValue<float>& smoothedGain,
                       int numSamples) noexcept;

    Filter bassFilter, midFilter, trebleFilter, presenceFilter;

    std::atomic<float> bassDb { 0.0f };
    std::atomic<float> midDb { 0.0f };
    std::atomic<float> trebleDb { 0.0f };
    std::atomic<float> presenceDb { 0.0f };

    juce::LinearSmoothedValue<float> bassGain { 1.0f };
    juce::LinearSmoothedValue<float> midGain { 1.0f };
    juce::LinearSmoothedValue<float> trebleGain { 1.0f };
    juce::LinearSmoothedValue<float> presenceGain { 1.0f };

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PostEq)
};

} // namespace sg
