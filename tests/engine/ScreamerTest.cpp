// Catch2 v3 tests for sg::Screamer (engine/include/sg/Screamer.h).
//
// API contract exercised here (per the M2 pedals brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough
//   void setDrive/setTone/setLevel(float) noexcept;  // normalized 0..1
//   int getLatencySamples() const noexcept;
//
// Drive/tone are probed with simple bin-aligned single-frequency DFT
// correlation (not a full FFT) -- exact for a steady-state pure sine, and
// plenty to demonstrate that harmonic content grows with drive and that the
// post-clip tone control shapes the high end, without pulling in an FFT
// dependency just for tests.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/Screamer.h>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    // Feeds a continuous sine through fx, in blocks no larger than
    // kMaxBlockSize, discarding `settleSamples` first (letting smoothed
    // params / oversampler / filters reach steady state), then returns the
    // following `measureSamples` of output as a flat buffer (mono).
    std::vector<float> settleAndCapture (sg::Screamer& fx, double frequencyHz, float amplitude,
                                          int settleSamples, int measureSamples)
    {
        juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
        const double phaseInc = juce::MathConstants<double>::twoPi * frequencyHz / kSampleRate;
        double phase = 0.0;

        auto feed = [&] (int numSamplesToFeed, std::vector<float>* out)
        {
            int remaining = numSamplesToFeed;
            while (remaining > 0)
            {
                const int thisBlock = std::min (remaining, kMaxBlockSize);
                buffer.setSize (1, thisBlock, false, false, true);

                for (int i = 0; i < thisBlock; ++i)
                {
                    buffer.setSample (0, i, (float) (amplitude * std::sin (phase)));
                    phase += phaseInc;
                }

                fx.process (buffer);

                if (out != nullptr)
                    for (int i = 0; i < thisBlock; ++i)
                        out->push_back (buffer.getSample (0, i));

                remaining -= thisBlock;
            }
        };

        feed (settleSamples, nullptr);

        std::vector<float> captured;
        captured.reserve ((size_t) measureSamples);
        feed (measureSamples, &captured);
        return captured;
    }

    // Bin-aligned single-frequency DFT: exact for a steady-state pure sine
    // when frequencyHz * samples.size() / sampleRate is (close to) an
    // integer. Returns { fundamentalPower, totalPower }.
    struct SpectralProbe
    {
        double fundamentalPower;
        double totalPower;
        double nonFundamentalPower() const { return std::max (0.0, totalPower - fundamentalPower); }
    };

    SpectralProbe probe (const std::vector<float>& samples, double frequencyHz, double sampleRate)
    {
        const auto n = (double) samples.size();
        double sumCos = 0.0, sumSin = 0.0, sumSquares = 0.0;

        for (size_t i = 0; i < samples.size(); ++i)
        {
            const double theta = juce::MathConstants<double>::twoPi * frequencyHz * (double) i / sampleRate;
            const double x = (double) samples[i];
            sumCos += x * std::cos (theta);
            sumSin += x * std::sin (theta);
            sumSquares += x * x;
        }

        const double a = 2.0 * sumCos / n;
        const double b = 2.0 * sumSin / n;

        SpectralProbe result;
        result.fundamentalPower = 0.5 * (a * a + b * b);
        result.totalPower = sumSquares / n;
        return result;
    }

    constexpr int kSettleSamples = 24000; // 0.5s @ 48kHz
    constexpr int kMeasureSamples = 4800; // 10 cycles @ 200Hz -- bin-aligned
}

TEST_CASE ("sg::Screamer disabled is a bit-exact passthrough", "[Screamer]")
{
    sg::Screamer fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setDrive (0.9f);
    fx.setTone (0.9f);
    fx.setLevel (1.0f);
    fx.setEnabled (false);
    fx.reset();

    juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample (0, i, (float) std::sin (0.3 * i) * 0.6f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    fx.process (buffer);

    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK (buffer.getSample (0, i) == reference.getSample (0, i));

    CHECK (fx.isEnabled() == false);
}

TEST_CASE ("sg::Screamer enabled with neutral-ish settings is finite, non-silent and deterministic", "[Screamer]")
{
    auto run = [] () {
        sg::Screamer fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setDrive (0.4f);
        fx.setTone (0.5f);
        fx.setLevel (sg::Screamer::unityLevelV01);
        fx.reset();
        return settleAndCapture (fx, 220.0, 0.3f, kSettleSamples, kMeasureSamples);
    };

    const auto a = run();
    const auto b = run();

    REQUIRE (a.size() == b.size());

    double sumSquares = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (std::isfinite (a[i]));
        CHECK (a[i] == b[i]); // same params, same input -> bit-identical, deterministic output
        sumSquares += (double) a[i] * (double) a[i];
    }

    const double rms = std::sqrt (sumSquares / (double) a.size());
    CHECK (rms > 1.0e-4); // non-silent
}

TEST_CASE ("sg::Screamer raising drive grows harmonic content relative to the fundamental", "[Screamer]")
{
    auto measureNonFundamentalRatio = [] (float drive01) {
        sg::Screamer fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setDrive (drive01);
        fx.setTone (0.6f);
        fx.setLevel (sg::Screamer::unityLevelV01);
        fx.reset();

        const auto captured = settleAndCapture (fx, 200.0, 0.35f, kSettleSamples, kMeasureSamples);
        const auto spectrum = probe (captured, 200.0, kSampleRate);
        return spectrum.nonFundamentalPower() / std::max (spectrum.totalPower, 1.0e-12);
    };

    const double lowDriveRatio = measureNonFundamentalRatio (0.0f);  // minimum (1x) drive gain
    const double highDriveRatio = measureNonFundamentalRatio (1.0f); // maximum (40x) drive gain

    CHECK (highDriveRatio > lowDriveRatio);
    CHECK (highDriveRatio > lowDriveRatio * 2.0); // substantially more distorted, not just noise-level drift
}

TEST_CASE ("sg::Screamer tone control brightens/darkens the post-clip signal", "[Screamer]")
{
    auto measureHighFreqRms = [] (float tone01) {
        sg::Screamer fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setDrive (0.3f);
        fx.setTone (tone01);
        fx.setLevel (sg::Screamer::unityLevelV01);
        fx.reset();

        // Feed a high-ish frequency straddling the tone sweep's range
        // (1kHz..8kHz) so tone=0 (1kHz corner) attenuates it heavily while
        // tone=1 (8kHz corner) passes it through close to unaffected.
        const auto captured = settleAndCapture (fx, 4000.0, 0.2f, kSettleSamples, kMeasureSamples);

        double sumSquares = 0.0;
        for (float s : captured)
            sumSquares += (double) s * (double) s;
        return std::sqrt (sumSquares / (double) captured.size());
    };

    const double darkRms = measureHighFreqRms (0.0f);
    const double brightRms = measureHighFreqRms (1.0f);

    CHECK (brightRms > darkRms * 3.0); // clearly brighter with tone all the way up
}

TEST_CASE ("sg::Screamer reports a small, stable oversampling latency", "[Screamer]")
{
    sg::Screamer fx;
    CHECK (fx.getLatencySamples() == 0); // before prepare()

    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    const int latency = fx.getLatencySamples();

    CHECK (latency > 0);
    CHECK (latency < 256); // sane upper bound for a 2x halfband oversampler at 48kHz

    fx.reset();
    CHECK (fx.getLatencySamples() == latency); // stable across reset()
}
