// Catch2 v3 tests for sg::StereoDelay (engine/include/sg/StereoDelay.h).
//
// API contract exercised here (per the M2 pedals brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough
//   void setTime/setFeedback/setMix(float) noexcept;  // normalized 0..1
//
// setTime() is documented to log-map [0,1] to [minTimeMs, maxTimeMs]; these
// tests compute the expected delay directly from those published constants
// rather than reimplementing an assumed formula.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/StereoDelay.h>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    // Log-mapped 60ms..1200ms, matching sg::StereoDelay::setTime()'s
    // documented behaviour and its published min/max constants.
    float expectedTimeMs (float v01)
    {
        return sg::StereoDelay::minTimeMs
             * std::pow (sg::StereoDelay::maxTimeMs / sg::StereoDelay::minTimeMs, v01);
    }

    std::vector<float> runMono (sg::StereoDelay& fx, const std::vector<float>& input)
    {
        std::vector<float> output;
        output.reserve (input.size());

        juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
        size_t pos = 0;
        while (pos < input.size())
        {
            const int thisBlock = (int) std::min<size_t> (kMaxBlockSize, input.size() - pos);
            buffer.setSize (1, thisBlock, false, false, true);

            for (int i = 0; i < thisBlock; ++i)
                buffer.setSample (0, i, input[pos + (size_t) i]);

            fx.process (buffer);

            for (int i = 0; i < thisBlock; ++i)
                output.push_back (buffer.getSample (0, i));

            pos += (size_t) thisBlock;
        }
        return output;
    }

    std::vector<float> impulse (size_t length)
    {
        std::vector<float> v (length, 0.0f);
        if (! v.empty())
            v[0] = 1.0f;
        return v;
    }

    // Largest |value| within [centre - window, centre + window] of `data`.
    float windowedPeak (const std::vector<float>& data, int centre, int window)
    {
        float peak = 0.0f;
        const int lo = std::max (0, centre - window);
        const int hi = std::min ((int) data.size() - 1, centre + window);
        for (int i = lo; i <= hi; ++i)
            peak = std::max (peak, std::abs (data[(size_t) i]));
        return peak;
    }
}

TEST_CASE ("sg::StereoDelay disabled is a bit-exact passthrough", "[StereoDelay]")
{
    sg::StereoDelay fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setTime (0.5f);
    fx.setFeedback (0.6f);
    fx.setMix (0.8f);
    fx.setEnabled (false);
    fx.reset();

    juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample (0, i, (float) std::sin (0.2 * i) * 0.7f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    fx.process (buffer);

    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK (buffer.getSample (0, i) == reference.getSample (0, i));

    CHECK (fx.isEnabled() == false);
}

TEST_CASE ("sg::StereoDelay enabled with neutral-ish settings is finite, non-silent and deterministic", "[StereoDelay]")
{
    auto run = [] () {
        sg::StereoDelay fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setTime (0.3f);
        fx.setFeedback (0.3f);
        fx.setMix (0.5f);
        fx.reset();

        std::vector<float> input (8000);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / kSampleRate) * 0.3f;

        return runMono (fx, input);
    };

    const auto a = run();
    const auto b = run();

    REQUIRE (a.size() == b.size());

    double sumSquares = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (std::isfinite (a[i]));
        CHECK (a[i] == b[i]); // same params, same input -> deterministic
        sumSquares += (double) a[i] * (double) a[i];
    }

    CHECK (std::sqrt (sumSquares / (double) a.size()) > 1.0e-4); // non-silent
}

TEST_CASE ("sg::StereoDelay impulse produces a single echo at the expected sample offset", "[StereoDelay]")
{
    sg::StereoDelay fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setTime (0.4f);
    fx.setFeedback (0.0f); // single echo only
    fx.setMix (1.0f);      // fully wet -- isolates the echo from the dry tap
    fx.reset();

    const float timeMs = expectedTimeMs (0.4f);
    const int expectedOffset = (int) std::lround (timeMs * 0.001 * kSampleRate);

    const auto output = runMono (fx, impulse ((size_t) expectedOffset + 200));

    int peakIndex = 0;
    float peakValue = 0.0f;
    for (int i = 0; i < (int) output.size(); ++i)
    {
        if (std::abs (output[(size_t) i]) > peakValue)
        {
            peakValue = std::abs (output[(size_t) i]);
            peakIndex = i;
        }
    }

    CHECK (peakIndex == Approx ((double) expectedOffset).margin (1.0));
    CHECK (peakValue > 0.5f); // most of the impulse's energy lands on a single interpolated sample
}

TEST_CASE ("sg::StereoDelay feedback produces geometrically decaying repeats", "[StereoDelay]")
{
    sg::StereoDelay fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setTime (0.25f);
    fx.setFeedback (0.5f); // -> loop gain 0.5 * 0.85 = 0.425
    fx.setMix (1.0f);
    fx.reset();

    const float timeMs = expectedTimeMs (0.25f);
    const int delaySamples = (int) std::lround (timeMs * 0.001 * kSampleRate);
    const float feedbackGain = 0.5f * sg::StereoDelay::maxFeedbackGain;

    const auto output = runMono (fx, impulse ((size_t) delaySamples * 4 + 200));

    const int window = 5; // tolerate a little smearing from the in-loop lowpass/interpolation
    const float peak1 = windowedPeak (output, delaySamples * 1, window);
    const float peak2 = windowedPeak (output, delaySamples * 2, window);
    const float peak3 = windowedPeak (output, delaySamples * 3, window);

    REQUIRE (peak1 > 0.0f);

    // Strictly decaying...
    CHECK (peak2 < peak1);
    CHECK (peak3 < peak2);

    // ...at roughly (not exactly, because of the in-loop lowpass) the
    // configured feedback ratio -- generous band to allow for that loss.
    const double ratio21 = (double) peak2 / (double) peak1;
    const double ratio32 = (double) peak3 / (double) peak2;

    CHECK (ratio21 < (double) feedbackGain * 1.1);
    CHECK (ratio21 > (double) feedbackGain * 0.3);
    CHECK (ratio32 < (double) feedbackGain * 1.1);
    CHECK (ratio32 > (double) feedbackGain * 0.3);
}

TEST_CASE ("sg::StereoDelay time slew produces no discontinuity above a click threshold", "[StereoDelay]")
{
    sg::StereoDelay fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setTime (0.1f);
    fx.setFeedback (0.0f);
    fx.setMix (1.0f);
    fx.reset();

    constexpr double freqHz = 300.0;
    constexpr float amplitude = 0.5f;

    auto sineInput = [&] (size_t startSampleIndex, size_t length) {
        std::vector<float> v (length);
        for (size_t i = 0; i < length; ++i)
        {
            const double n = (double) (startSampleIndex + i);
            v[i] = (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * freqHz * n / kSampleRate));
        }
        return v;
    };

    // Fill the delay line's history with steady-state signal first.
    const auto fillInput = sineInput (0, 4000);
    runMono (fx, fillInput);

    // Trigger a realistic knob-drag time jump (no reset() -- this must
    // slew): roughly a third of the full 60ms..1200ms range. Note this
    // deliberately stops short of a full-range (e.g. 0.0 -> 1.0) jump: at
    // this delay-time smoothing rate (60ms..1200ms range / 50ms ramp),
    // slamming the knob across the *entire* range in one go moves the read
    // tap through recorded history faster than real-time by more than an
    // order of magnitude, which produces an audible (and expected/accepted,
    // like tape-echo "speed up") pitch-glide through the source signal --
    // that's inherent to how far/fast the tap is asked to move, not a
    // stairstep discontinuity from missing smoothing. What we're testing
    // for here is the latter: that a normal, still-substantial knob move
    // is slewed smoothly rather than jumping instantly.
    fx.setTime (0.5f);

    // Continue feeding the same continuous signal through and past the
    // ~50ms slew window, watching for any excessive sample-to-sample jump.
    const int slewWindowSamples = (int) (kSampleRate * 0.05) * 3; // 3x the smoothing time, generous
    const auto duringSlew = sineInput (fillInput.size(), (size_t) slewWindowSamples);
    const auto output = runMono (fx, duringSlew);

    float maxStep = 0.0f;
    for (size_t i = 1; i < output.size(); ++i)
        maxStep = std::max (maxStep, std::abs (output[i] - output[i - 1]));

    // A genuine zipper/discontinuity from an unsmoothed jump this size
    // would show up as a step on the order of the full signal swing (the
    // read tap would land on a completely uncorrelated point in the
    // sine's history); the input's own natural sample-to-sample step at
    // 300Hz/0.5 amplitude is roughly 0.02, so this threshold has ample
    // headroom above normal signal movement while still catching a real
    // click.
    CHECK (maxStep < 0.15f);
}
