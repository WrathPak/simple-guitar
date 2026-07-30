// Catch2 v3 tests for sg::Gain (engine/include/sg/Gain.h) and sg::MeterTap
// (engine/include/sg/MeterTap.h), per the M0 bridge PoC ("one React knob
// controls output gain of audio passthrough, meter data flows back" -
// PLAN.md §4 M0).
//
// API contract exercised here (confirmed with the engine workstream):
//   sg::Gain
//     void prepare(double sampleRate, int maximumBlockSize, int numChannels) noexcept;
//     void reset() noexcept;                       // snaps to current target, no ramp
//     void setGainDecibels(float newGainDb) noexcept;
//     float getGainDecibels() const noexcept;       // returns the target, not the ramping value
//     void process(juce::dsp::AudioBlock<float>& block) noexcept;
//     // Fixed internal smoothing time: 20ms (static constexpr smoothingTimeSeconds = 0.02).
//
//   sg::MeterTap
//     static constexpr int maxChannels = 2;
//     MeterTap() noexcept;                          // pre-zeroed
//     void reset() noexcept;
//     void pushBlock(const float* const* channelData, int numChannels, int numSamples) noexcept;
//     float getPeak(int channel) const noexcept;     // linear peak magnitude, 0 for out-of-range channel

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <sg/Gain.h>
#include <sg/MeterTap.h>

#include <algorithm>
#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    float dbToGain(float db)
    {
        return juce::Decibels::decibelsToGain(db);
    }

    // Feeds `numSamples` total samples of a constant 1.0f input through
    // `gain`, in blocks no larger than kMaxBlockSize (matching what was
    // passed to prepare()), and returns the value of the very last sample
    // processed -- i.e. the ramp's value after that many samples have
    // elapsed since the last setGainDecibels() call.
    float runAndGetLastSample(sg::Gain& gain, int numSamples)
    {
        REQUIRE(numSamples > 0);
        juce::AudioBuffer<float> buffer(1, kMaxBlockSize);
        float lastSample = 0.0f;

        int remaining = numSamples;
        while (remaining > 0)
        {
            const int thisBlock = std::min(remaining, kMaxBlockSize);
            buffer.setSize(1, thisBlock, false, false, true);
            for (int i = 0; i < thisBlock; ++i)
                buffer.setSample(0, i, 1.0f);

            juce::dsp::AudioBlock<float> block(buffer);
            gain.process(block);

            lastSample = buffer.getSample(0, thisBlock - 1);
            remaining -= thisBlock;
        }
        return lastSample;
    }
}

TEST_CASE("sg::Gain at 0 dB is unity passthrough", "[Gain]")
{
    sg::Gain gain;
    gain.prepare(kSampleRate, kMaxBlockSize, 1);
    gain.setGainDecibels(0.0f);
    gain.reset(); // snap immediately, no ramp

    juce::AudioBuffer<float> buffer(1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample(0, i, 0.5f);

    juce::dsp::AudioBlock<float> block(buffer);
    gain.process(block);

    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK(buffer.getSample(0, i) == Approx(0.5f).margin(1.0e-6));

    CHECK(gain.getGainDecibels() == Approx(0.0f));
}

TEST_CASE("sg::Gain at -60 dB attenuates accordingly", "[Gain]")
{
    sg::Gain gain;
    gain.prepare(kSampleRate, kMaxBlockSize, 1);
    gain.setGainDecibels(-60.0f);
    gain.reset(); // snap immediately, so this block is already at target

    juce::AudioBuffer<float> buffer(1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample(0, i, 1.0f);

    juce::dsp::AudioBlock<float> block(buffer);
    gain.process(block);

    const float expected = dbToGain(-60.0f); // 0.001
    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK(buffer.getSample(0, i) == Approx(expected).margin(1.0e-6));

    CHECK(gain.getGainDecibels() == Approx(-60.0f));
}

TEST_CASE("sg::Gain smoothing reaches target within 50ms of samples at 48kHz", "[Gain]")
{
    sg::Gain gain;
    gain.prepare(kSampleRate, kMaxBlockSize, 1);
    gain.setGainDecibels(0.0f);
    gain.reset(); // start settled at unity

    gain.setGainDecibels(-60.0f); // begin ramping (no reset() this time)
    const float target = dbToGain(-60.0f);

    // Partway through the ramp (well under the engine's fixed 20ms ramp
    // time -> 960 samples at 48kHz) the output should NOT yet equal the
    // target. This proves we're observing an actual smoothed ramp rather
    // than an instant jump to the new gain.
    const float midRampSample = runAndGetLastSample(gain, kMaxBlockSize); // 128 samples in
    CHECK(std::abs(midRampSample - target) > 1.0e-3f);
    CHECK(midRampSample < 1.0f);
    CHECK(midRampSample > target);

    // 50ms at 48kHz = 2400 samples. Regardless of the engine's exact
    // internal ramp time, the smoothing acceptance target is that it has
    // fully converged by 50ms.
    const int samplesFor50ms = static_cast<int>(kSampleRate * 0.050);
    const float finalSample = runAndGetLastSample(gain, samplesFor50ms - kMaxBlockSize);

    CHECK(finalSample == Approx(target).margin(1.0e-4));
    CHECK(gain.getGainDecibels() == Approx(-60.0f)); // target readback is unaffected by ramp position
}

TEST_CASE("sg::MeterTap reports the peak of a known buffer", "[MeterTap]")
{
    sg::MeterTap tap;

    constexpr int numChannels = 2;
    constexpr int numSamples = 4;

    float channel0[numSamples] = { 0.1f, -0.9f, 0.3f, 0.2f };
    float channel1[numSamples] = { 0.2f, 0.05f, -0.5f, 0.05f };
    const float* channelData[numChannels] = { channel0, channel1 };

    tap.pushBlock(channelData, numChannels, numSamples);

    CHECK(tap.getPeak(0) == Approx(0.9f));  // |-0.9| is the loudest sample on ch0
    CHECK(tap.getPeak(1) == Approx(0.5f));  // |-0.5| is the loudest sample on ch1

    // Out-of-range channel reports 0, not garbage/UB.
    CHECK(tap.getPeak(2) == Approx(0.0f));

    tap.reset();
    CHECK(tap.getPeak(0) == Approx(0.0f));
    CHECK(tap.getPeak(1) == Approx(0.0f));
}
