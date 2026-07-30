#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    Plate/room reverb ("chamber"): a juce::dsp::Reverb (Freeverb-derived)
    engine run at 100% wet internally, combined with the dry signal outside
    it via an equal-power mix so mix = 0 is guaranteed bit-transparent dry.

    setDecay()/setTone()/setMix()/setEnabled() are safe to call from any
    thread (atomic store only) and take normalized [0, 1] values; the
    mapping to real units happens internally. process() must be called from
    the audio thread: decay/tone changes are smoothed internally by the
    underlying juce::dsp::Reverb, and mix is smoothed per-sample on top, so
    parameter moves never produce zipper noise.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate (it sizes the dry-signal scratch buffer) and must
    be called from a non-audio thread before processing starts (or whenever
    the sample rate/block size/channel count changes).
*/
class PlateReverb
{
public:
    PlateReverb() = default;

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, or when the mix is settled at 0, this is
        a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the reverb's internal state, and snaps the smoothed mix to
        its current target with no ramp. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the decay, normalized [0, 1]: short room (0) to long plate (1).
        Thread-safe (atomic store). Maps to the underlying reverb's room
        size (plus a touch of stereo width for longer settings). */
    void setDecay (float v01) noexcept;

    /** Sets the tone, normalized [0, 1]: dark (0) to bright (1). Thread-safe
        (atomic store). Maps inversely to the underlying reverb's damping. */
    void setTone (float v01) noexcept;

    /** Sets the dry/wet mix, normalized [0, 1] (0 = dry, 1 = fully wet).
        Thread-safe (atomic store). Equal-power law; mix = 0 is guaranteed
        bit-exact dry. */
    void setMix (float v01) noexcept;

    /** Returns the most recently set normalized targets (not the ramping
        values). */
    float getDecay() const noexcept { return decay01.load (std::memory_order_relaxed); }
    float getTone() const noexcept { return tone01.load (std::memory_order_relaxed); }
    float getMix() const noexcept { return mix01.load (std::memory_order_relaxed); }

    /** Decay maps to the underlying reverb's room size in this range. */
    static constexpr float minRoomSize = 0.10f;
    static constexpr float maxRoomSize = 0.98f;

    /** Decay also nudges stereo width across this range (a touch wider for
        longer decay settings). */
    static constexpr float minWidth = 0.5f;
    static constexpr float maxWidth = 1.0f;

    /** Tone maps inversely to the underlying reverb's damping across this
        range (0 = dark/high damping, 1 = bright/low damping). */
    static constexpr float maxDamping = 0.90f;
    static constexpr float minDamping = 0.05f;

    /** Smoothing time for mix changes, in seconds. */
    static constexpr double mixSmoothingTimeSeconds = 0.02;

private:
    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;

    std::atomic<bool> enabled { true };
    std::atomic<float> decay01 { 0.5f };
    std::atomic<float> tone01 { 0.5f };
    std::atomic<float> mix01 { 0.35f };

    juce::LinearSmoothedValue<float> smoothedMix { 0.0f };

    void applyReverbParameters() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlateReverb)
};

} // namespace sg
