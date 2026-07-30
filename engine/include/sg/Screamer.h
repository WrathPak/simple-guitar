#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace sg
{

/**
    TS-style overdrive ("screamer"): a fixed pre-clip mid-focused shaping
    network (a highpass into the clipper, blended with a touch of the dry
    low end for punch) feeding a smooth tanh soft-clipper running at 2x
    oversampling to control aliasing, followed by a post-clip tone tilt and
    an output level stage.

    setDrive()/setTone()/setLevel()/setEnabled() are safe to call from any
    thread (atomic store only) and take normalized [0, 1] values; the mapping
    to real units happens internally. process() must be called from the
    audio thread: it ramps drive and level smoothly (no zipper noise) and
    recomputes the tone filter's coefficients once per block from a smoothed
    target, so parameter moves never produce discontinuities.

    Realtime-safety: process() and the setters never lock or allocate.
    prepare() may allocate (it (re)builds the oversampler and scratch
    buffers) and must be called from a non-audio thread before processing
    starts (or whenever the sample rate/block size/channel count changes).

    Oversampling adds a small, fixed processing latency once prepare() has
    run; call getLatencySamples() to report it (e.g. for host PDC).
*/
class Screamer
{
public:
    Screamer();
    ~Screamer();

    /** Prepares the processor for a given sample rate, block size and
        channel count. May allocate; do not call from the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /** Processes an audio buffer in place. Audio-thread only: no locks, no
        allocation. When disabled, this is a bit-exact passthrough. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Resets the oversampler and filters' internal state, and snaps all
        smoothed parameters to their current targets with no ramp. */
    void reset() noexcept;

    /** Enables or disables the pedal (the footswitch). Thread-safe (atomic
        store). When disabled, process() is a clean, bit-exact passthrough. */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns whether the pedal is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    /** Sets the drive amount, normalized [0, 1]. Thread-safe (atomic
        store). Maps exponentially to a pre-clip gain of roughly 1x (0.0) to
        40x (1.0) so the control feels perceptually even across its range. */
    void setDrive (float v01) noexcept;

    /** Sets the post-clip tone, normalized [0, 1]. Thread-safe (atomic
        store). 0 = dark, 1 = bright: sweeps a post-clip lowpass corner
        roughly from 1 kHz to 8 kHz (log-mapped). */
    void setTone (float v01) noexcept;

    /** Sets the output level, normalized [0, 1]. Thread-safe (atomic
        store). 0 is very quiet (approximately -60 dB), 0.7 is unity gain,
        1.0 is roughly +6 dB. */
    void setLevel (float v01) noexcept;

    /** Returns the most recently set normalized targets (not the ramping
        values). */
    float getDrive() const noexcept { return drive01.load (std::memory_order_relaxed); }
    float getTone() const noexcept { return tone01.load (std::memory_order_relaxed); }
    float getLevel() const noexcept { return level01.load (std::memory_order_relaxed); }

    /** Returns the fixed processing latency introduced by the internal 2x
        oversampling, in samples at the base (non-oversampled) sample rate.
        Valid once prepare() has run; 0 before that. */
    int getLatencySamples() const noexcept;

    /** Oversampling factor (2x) used to control aliasing in the clipper. */
    static constexpr int oversamplingFactorLog2 = 1; // 2 ^ 1 = 2x

    /** Drive maps to a pre-clip gain in [minDriveGain, maxDriveGain]. */
    static constexpr float minDriveGain = 1.0f;
    static constexpr float maxDriveGain = 40.0f;

    /** Tone sweeps a post-clip lowpass corner in [minToneHz, maxToneHz]. */
    static constexpr float minToneHz = 1000.0f;
    static constexpr float maxToneHz = 8000.0f;

    /** Level maps to output gain in [minLevelDb, maxLevelDb]; 0.7 ~ unity. */
    static constexpr float minLevelDb = -60.0f;
    static constexpr float unityLevelDb = 0.0f;
    static constexpr float maxLevelDb = 6.0f;
    static constexpr float unityLevelV01 = 0.7f;

    /** Fixed pre-clip network: highpass corner feeding the clipper, and the
        fraction of the dry lowpassed signal blended back in ahead of the
        clipper to retain some low-end punch (classic TS-style behaviour). */
    static constexpr float preHighpassHz = 720.0f;
    static constexpr float preFilterQ = 0.70710678f; // Butterworth (maximally flat)
    static constexpr float dryLowBlend = 0.25f;

    /** Smoothing times, in seconds. */
    static constexpr double driveSmoothingTimeSeconds = 0.03;
    static constexpr double toneSmoothingTimeSeconds = 0.05;
    static constexpr double levelSmoothingTimeSeconds = 0.02;

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                   juce::dsp::IIR::Coefficients<float>>;

    static float mapDriveGain (float v01) noexcept;
    static float mapToneHz (float v01) noexcept;
    static float mapLevelDb (float v01) noexcept;

    void snapToneFilter() noexcept;
    void advanceToneFilter (int numSamples) noexcept;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    Filter preHighpassFilter, preLowpassFilter, toneFilter;

    // Scratch buffers sized in prepare(); never (re)allocated in process().
    juce::AudioBuffer<float> highpassScratch, lowpassScratch;
    std::vector<float> driveGainPerSample;

    std::atomic<bool> enabled { true };
    std::atomic<float> drive01 { 0.4f };
    std::atomic<float> tone01 { 0.5f };
    std::atomic<float> level01 { unityLevelV01 };

    juce::LinearSmoothedValue<float> smoothedDriveGain { minDriveGain };
    juce::LinearSmoothedValue<float> smoothedToneHz { maxToneHz * 0.5f };
    juce::LinearSmoothedValue<float> smoothedOutputGain { 1.0f };

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Screamer)
};

} // namespace sg
