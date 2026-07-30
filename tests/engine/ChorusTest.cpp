// Catch2 v3 tests for sg::Chorus (engine/include/sg/Chorus.h).
//
// API contract exercised here (per the M2 slot-architecture brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough
//   void setRate/setDepth/setMix(float) noexcept;  // normalized 0..1
//
// The headline behaviour under test ("chorus modulates delay audibly") is
// verified in the time domain rather than via an FFT: a chorus is a
// continuously-modulated short delay, so a steady input tone comes out
// frequency-modulated -- its zero-crossing period jitters cycle to cycle,
// where a dry (or plain-delayed) tone's period is rock steady. That's a
// direct, robust proxy for "the delay is being modulated", without pulling
// in FFT/windowing machinery or picking arbitrary spectral-bin thresholds.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/Chorus.h>

#include <cmath>
#include <numeric>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    std::vector<float> runMono (sg::Chorus& fx, const std::vector<float>& input)
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

    std::vector<float> steadyTone (float amplitude, double freqHz, size_t length, double sampleRate)
    {
        std::vector<float> v (length);
        for (size_t i = 0; i < length; ++i)
            v[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freqHz * (double) i / sampleRate);
        return v;
    }

    // Rising zero-crossing periods (seconds), linearly interpolated for
    // sub-sample precision, measured only within [analysisStart, size).
    std::vector<double> risingZeroCrossPeriods (const std::vector<float>& signal, size_t analysisStart, double sampleRate)
    {
        std::vector<double> periods;
        double lastCrossing = -1.0;

        for (size_t i = std::max<size_t> (analysisStart, 1); i < signal.size(); ++i)
        {
            const float prev = signal[i - 1];
            const float cur = signal[i];

            if (prev < 0.0f && cur >= 0.0f)
            {
                const double frac = (cur == prev) ? 0.0 : (double) (-prev) / (double) (cur - prev);
                const double crossing = (double) (i - 1) + frac;

                if (lastCrossing >= 0.0)
                    periods.push_back ((crossing - lastCrossing) / sampleRate);

                lastCrossing = crossing;
            }
        }

        return periods;
    }

    double stddev (const std::vector<double>& v)
    {
        if (v.size() < 2)
            return 0.0;

        const double mean = std::accumulate (v.begin(), v.end(), 0.0) / (double) v.size();
        double sumSq = 0.0;
        for (auto x : v)
            sumSq += (x - mean) * (x - mean);

        return std::sqrt (sumSq / (double) (v.size() - 1));
    }
}

TEST_CASE ("sg::Chorus disabled is a bit-exact passthrough", "[Chorus]")
{
    sg::Chorus fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.8f);
    fx.setDepth (1.0f);
    fx.setMix (1.0f);
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

TEST_CASE ("sg::Chorus enabled is finite and non-silent", "[Chorus]")
{
    sg::Chorus fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.5f);
    fx.setDepth (0.5f);
    fx.setMix (0.5f);
    fx.reset();

    const auto output = runMono (fx, steadyTone (0.4f, 440.0, 20000, kSampleRate));

    double sumSquares = 0.0;
    for (auto s : output)
    {
        CHECK (std::isfinite (s));
        sumSquares += (double) s * (double) s;
    }

    CHECK (std::sqrt (sumSquares / (double) output.size()) > 1.0e-3);
}

TEST_CASE ("sg::Chorus frequency-modulates a steady tone -- period jitter well above dry", "[Chorus]")
{
    constexpr double toneHz = 300.0;
    constexpr size_t totalLength = (size_t) (kSampleRate * 2.0); // 2 seconds, ~600 cycles at 300Hz
    constexpr size_t analysisStart = (size_t) (kSampleRate * 0.2); // skip startup (delay line filling + smoothers)

    const auto dryInput = steadyTone (0.5f, toneHz, totalLength, kSampleRate);

    sg::Chorus fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setRate (0.7f);  // a few Hz, comfortably resolvable within the 2s analysis window
    fx.setDepth (1.0f); // maximum modulation depth
    fx.setMix (1.0f);   // fully wet -- isolates the modulated path from any dry blend
    fx.reset();

    const auto wetOutput = runMono (fx, dryInput);

    const auto dryPeriods = risingZeroCrossPeriods (dryInput, analysisStart, kSampleRate);
    const auto wetPeriods = risingZeroCrossPeriods (wetOutput, analysisStart, kSampleRate);

    REQUIRE (dryPeriods.size() > 100);
    REQUIRE (wetPeriods.size() > 100);

    const double dryJitter = stddev (dryPeriods);
    const double wetJitter = stddev (wetPeriods);

    // The dry tone's period is rock steady (only floating-point/interpolation
    // noise); the chorused tone's period visibly wobbles as the LFO sweeps
    // the delay -- a direct, robust proxy for "the delay is being modulated".
    CHECK (dryJitter < 5.0e-6);
    CHECK (wetJitter > dryJitter * 10.0);
    CHECK (wetJitter > 2.0e-5);
}
