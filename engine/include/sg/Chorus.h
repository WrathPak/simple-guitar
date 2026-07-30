#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Chorus ("wobble"): juce::dsp::Chorus (a modulated, short multi-voice
    delay line) with a fixed centre delay and feedback, driven by
    normalized rate/depth/mix controls.

    setRate()/setDepth()/setMix()/setEnabled() are safe to call from any
    thread (atomic store only) and take normalized [0, 1] values; the
    mapping to real units happens internally. process() must be called from
    the audio thread: juce::dsp::Chorus applies its own parameter changes
    smoothly internally, so simply re-applying the current targets once per
    block (same convention as PostEq/IrLoader elsewhere in this engine) never
    produces zipper noise or discontinuities.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate and must be called from a non-audio thread before
    processing starts (or whenever the sample rate/block size/channel count
    changes).
*/
class Chorus
{
public:
    Chorus();
    ~Chorus();

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the chorus's internal delay/LFO state. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the LFO rate, normalized [0, 1]. Thread-safe (atomic store).
        Log-mapped from 0.1Hz to 5Hz. */
    void setRate (float v01) noexcept;

    /** Sets the modulation depth, normalized [0, 1]. Thread-safe (atomic
        store). Passed straight through to juce::dsp::Chorus's own [0, 1]
        depth parameter. */
    void setDepth (float v01) noexcept;

    /** Sets the dry/wet mix, normalized [0, 1] (0 = dry, 1 = fully wet).
        Thread-safe (atomic store). Passed straight through to
        juce::dsp::Chorus's own [0, 1] mix parameter. */
    void setMix (float v01) noexcept;

    /** Returns the most recently set normalized targets. */
    float getRate() const noexcept { return rate01.load (std::memory_order_relaxed); }
    float getDepth() const noexcept { return depth01.load (std::memory_order_relaxed); }
    float getMix() const noexcept { return mix01.load (std::memory_order_relaxed); }

    /** Rate range, in Hz; log-mapped from setRate(). */
    static constexpr float minRateHz = 0.1f;
    static constexpr float maxRateHz = 5.0f;

    /** Fixed centre delay and feedback -- classic chorus territory, not
        exposed as separate controls. */
    static constexpr float centreDelayMs = 7.0f;
    static constexpr float feedback = 0.0f;

private:
    static float mapRateHz (float v01) noexcept;

    void applyChorusParameters() noexcept;

    juce::dsp::Chorus<float> chorus;

    std::atomic<bool> enabled { true };
    std::atomic<float> rate01 { 0.5f };
    std::atomic<float> depth01 { 0.5f };
    std::atomic<float> mix01 { 0.5f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chorus)
};

} // namespace sg
