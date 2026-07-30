// Catch2 v3 tests for sg::Tremolo (engine/include/sg/Tremolo.h).
//
// API contract exercised here (per the M2 slot-architecture brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough
//   void setRate/setDepth/setShape(float) noexcept;  // normalized 0..1
//
// Tremolo is a hand-rolled per-sample LFO gain modulator (no juce::dsp
// engine underneath), so its own behaviour -- amplitude-modulating at the
// configured rate -- is the thing under test here, measured directly: a
// constant (DC-ish) input reveals the LFO gain waveform itself, and its
// peak-to-peak period is compared against the rate setter's documented
// log-mapped Hz range.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/Tremolo.h>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    std::vector<float> runMono (sg::Tremolo& fx, const std::vector<float>& input)
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

    // Log-mapped 0.5Hz..12Hz, matching sg::Tremolo::setRate()'s documented
    // behaviour and its published min/max constants.
    float expectedRateHz (float v01)
    {
        return sg::Tremolo::minRateHz * std::pow (sg::Tremolo::maxRateHz / sg::Tremolo::minRateHz, v01);
    }

    // Indices of local maxima in `signal` within [start, size).
    std::vector<size_t> findPeaks (const std::vector<float>& signal, size_t start)
    {
        std::vector<size_t> peaks;
        for (size_t i = std::max<size_t> (start, 1); i + 1 < signal.size(); ++i)
            if (signal[i] > signal[i - 1] && signal[i] >= signal[i + 1])
                peaks.push_back (i);
        return peaks;
    }
}

TEST_CASE ("sg::Tremolo disabled is a bit-exact passthrough", "[Tremolo]")
{
    sg::Tremolo fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.8f);
    fx.setDepth (1.0f);
    fx.setShape (0.5f);
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
}

TEST_CASE ("sg::Tremolo depth 0 leaves the signal essentially untouched", "[Tremolo]")
{
    sg::Tremolo fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.7f);
    fx.setDepth (0.0f);
    fx.setShape (0.0f);
    fx.reset();

    std::vector<float> input (4000);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / kSampleRate);

    const auto output = runMono (fx, input);

    for (size_t i = 0; i < input.size(); ++i)
        CHECK (output[i] == Approx (input[i]).margin (1.0e-6));
}

TEST_CASE ("sg::Tremolo amplitude-modulates at approximately the configured rate", "[Tremolo]")
{
    sg::Tremolo fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);

    constexpr float rate01 = 0.6f;
    fx.setRate (rate01);
    fx.setDepth (1.0f);  // full swing 0..1 gain -> unambiguous, clean peaks
    fx.setShape (0.0f);  // pure sine -- exactly one peak per LFO cycle
    fx.reset();

    // Constant input reveals the LFO gain waveform directly.
    constexpr size_t length = (size_t) (kSampleRate * 2.0); // 2 seconds
    const std::vector<float> input (length, 1.0f);

    const auto output = runMono (fx, input);

    // Skip the rate/depth smoothers' short settling window (see
    // sg::Tremolo::rateSmoothingTimeSeconds/depthSmoothingTimeSeconds).
    const size_t analysisStart = (size_t) (kSampleRate * 0.2);
    const auto peaks = findPeaks (output, analysisStart);

    REQUIRE (peaks.size() > 5);

    const double measuredPeriodSeconds = (double) (peaks.back() - peaks.front()) / (double) (peaks.size() - 1) / kSampleRate;
    const double measuredRateHz = 1.0 / measuredPeriodSeconds;

    const double expected = (double) expectedRateHz (rate01);

    CHECK (measuredRateHz == Approx (expected).epsilon (0.1)); // within 10%
}

TEST_CASE ("sg::Tremolo output stays within [0, inputPeak] for full depth", "[Tremolo]")
{
    sg::Tremolo fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.5f);
    fx.setDepth (1.0f);
    fx.setShape (0.3f);
    fx.reset();

    constexpr float inputPeak = 0.8f;
    const std::vector<float> input ((size_t) (kSampleRate * 1.0), inputPeak);

    const auto output = runMono (fx, input);

    for (auto s : output)
    {
        CHECK (std::isfinite (s));
        CHECK (s >= -1.0e-6f);
        CHECK (s <= inputPeak + 1.0e-6f);
    }
}
