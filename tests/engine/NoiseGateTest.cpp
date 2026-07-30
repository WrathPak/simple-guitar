// Catch2 v3 tests for sg::NoiseGate (engine/include/sg/NoiseGate.h).
//
// API contract exercised here (per the M1 engine-blocks brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;   // audio thread safe
//   void reset() noexcept;
//   void setThresholdDb(float) noexcept;   // -90..-20
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/NoiseGate.h>

#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    float dbToGain (float db)
    {
        return juce::Decibels::decibelsToGain (db);
    }

    // Feeds a continuous sine wave of the given amplitude through the gate,
    // in blocks no larger than kMaxBlockSize, for numSamples total. Returns
    // the peak absolute sample value observed in the *last* block processed,
    // i.e. after numSamples have elapsed since this call started (on top of
    // whatever state the gate was already in).
    float feedSineAndGetLastBlockPeak (sg::NoiseGate& gate, double frequencyHz, float amplitude,
                                        int numSamples, double& phaseInOut)
    {
        juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
        const double phaseInc = juce::MathConstants<double>::twoPi * frequencyHz / kSampleRate;

        float lastBlockPeak = 0.0f;
        int remaining = numSamples;

        while (remaining > 0)
        {
            const int thisBlock = std::min (remaining, kMaxBlockSize);
            buffer.setSize (1, thisBlock, false, false, true);

            for (int i = 0; i < thisBlock; ++i)
            {
                buffer.setSample (0, i, (float) (amplitude * std::sin (phaseInOut)));
                phaseInOut += phaseInc;
            }

            gate.process (buffer);

            lastBlockPeak = 0.0f;
            for (int i = 0; i < thisBlock; ++i)
                lastBlockPeak = std::max (lastBlockPeak, std::abs (buffer.getSample (0, i)));

            remaining -= thisBlock;
        }

        return lastBlockPeak;
    }
}

TEST_CASE ("sg::NoiseGate passes a sine well above threshold", "[NoiseGate]")
{
    sg::NoiseGate gate;
    gate.prepare (kSampleRate, kMaxBlockSize, 1);
    gate.setThresholdDb (-40.0f);
    gate.reset();

    const float amplitude = 0.5f; // roughly -9 dBFS RMS, well above -40 dB threshold
    double phase = 0.0;

    // Run long enough for the envelope follower to settle into the "open" state.
    const float peak = feedSineAndGetLastBlockPeak (gate, 220.0, amplitude, 10000, phase);

    // Fully open, the gate applies unity gain -- output should track the input peak closely.
    CHECK (peak == Approx (amplitude).margin (0.02f));
}

TEST_CASE ("sg::NoiseGate attenuates heavily once a below-threshold signal settles through release", "[NoiseGate]")
{
    sg::NoiseGate gate;
    gate.prepare (kSampleRate, kMaxBlockSize, 1);
    gate.setThresholdDb (-40.0f);
    gate.reset();

    double phase = 0.0;

    // Phase 1: open the gate with a loud signal.
    const float loudAmplitude = 0.5f;
    feedSineAndGetLastBlockPeak (gate, 220.0, loudAmplitude, 5000, phase);

    // Phase 2: drop to a signal ~20 dB below the threshold and let the
    // (fixed, ~120ms) release fully settle -- run for several release times.
    const float quietAmplitude = dbToGain (-60.0f); // threshold(-40) - 20dB
    const float settledPeak = feedSineAndGetLastBlockPeak (gate, 220.0, quietAmplitude, 20000, phase);

    // Gain when fully closed is pow(env/threshold, ratio-1) with env << threshold,
    // i.e. many orders of magnitude of attenuation -- output should be near silence,
    // certainly far below the already-quiet input amplitude.
    CHECK (settledPeak < quietAmplitude * 0.1f);
}

TEST_CASE ("sg::NoiseGate disabled is a bit-exact passthrough", "[NoiseGate]")
{
    sg::NoiseGate gate;
    gate.prepare (kSampleRate, kMaxBlockSize, 1);
    gate.setThresholdDb (-20.0f); // would otherwise slam a quiet signal shut
    gate.setEnabled (false);
    gate.reset();

    juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample (0, i, (float) std::sin (0.3 * i) * 0.0001f); // tiny, would be gated hard if enabled

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    gate.process (buffer);

    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK (buffer.getSample (0, i) == reference.getSample (0, i));

    CHECK (gate.isEnabled() == false);
}
