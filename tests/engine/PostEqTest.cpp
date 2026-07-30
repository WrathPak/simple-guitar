// Catch2 v3 tests for sg::PostEq (engine/include/sg/PostEq.h).
//
// API contract exercised here (per the M1 engine-blocks brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setBassDb/setMidDb/setTrebleDb/setPresenceDb(float) noexcept;   // -12..+12 dB
//
// Fixed band centres (see sg::PostEq): bass ~100 Hz low shelf, mid ~750 Hz
// peak (Q ~0.8), treble ~3 kHz high shelf, presence ~6.5 kHz high shelf.
//
// These are magnitude-response *sanity* checks via steady-state sine probes,
// not exact analytic transfer-function comparisons -- they're deliberately
// tolerant of the precise shelf/peak slope math, but tight enough to catch a
// band being wired to the wrong filter type, the wrong frequency, or not
// applied at all.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/PostEq.h>

#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    // Feeds a continuous sine of the given frequency/amplitude through eq,
    // discarding `settleSamples` to let the (smoothed, IIR) filters reach
    // steady state, then measures the RMS of the following `measureSamples`.
    double steadyStateRms (sg::PostEq& eq, double frequencyHz, float amplitude,
                            int settleSamples, int measureSamples)
    {
        juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
        const double phaseInc = juce::MathConstants<double>::twoPi * frequencyHz / kSampleRate;
        double phase = 0.0;

        auto feed = [&] (int numSamplesToFeed, double* sumSquaresOut)
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

                eq.process (buffer);

                if (sumSquaresOut != nullptr)
                {
                    for (int i = 0; i < thisBlock; ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        *sumSquaresOut += (double) s * (double) s;
                    }
                }

                remaining -= thisBlock;
            }
        };

        feed (settleSamples, nullptr);

        double sumSquares = 0.0;
        feed (measureSamples, &sumSquares);

        return std::sqrt (sumSquares / (double) measureSamples);
    }

    double gainToDb (double ratio)
    {
        return 20.0 * std::log10 (std::max (ratio, 1.0e-12));
    }

    constexpr int kSettleSamples = 24000; // 0.5s @ 48kHz -- well past the 50ms gain ramp
    constexpr int kMeasureSamples = 24000;
}

TEST_CASE ("sg::PostEq +12dB bass boosts a 60Hz sine and leaves 5kHz nearly unchanged", "[PostEq]")
{
    sg::PostEq eq;
    eq.prepare (kSampleRate, kMaxBlockSize, 1);
    eq.setBassDb (12.0f);
    eq.reset();

    const float amplitude = 0.2f;
    const double inputRms = amplitude / std::sqrt (2.0);

    const double lowRms = steadyStateRms (eq, 60.0, amplitude, kSettleSamples, kMeasureSamples);
    const double lowBoostDb = gainToDb (lowRms / inputRms);

    // 60Hz sits below the ~100Hz shelf corner (in the boosted region) but not
    // deep enough into the shelf to see the full +12dB -- sanity-check it's a
    // substantial, plausible boost strictly less than the +12dB ceiling.
    CHECK (lowBoostDb > 6.0);
    CHECK (lowBoostDb < 12.5);

    eq.reset();
    const double highRms = steadyStateRms (eq, 5000.0, amplitude, kSettleSamples, kMeasureSamples);
    const double highBoostDb = gainToDb (highRms / inputRms);

    CHECK (highBoostDb == Approx (0.0).margin (1.0));
}

TEST_CASE ("sg::PostEq +12dB mid peak boosts 750Hz and leaves 80Hz nearly unchanged", "[PostEq]")
{
    sg::PostEq eq;
    eq.prepare (kSampleRate, kMaxBlockSize, 1);
    eq.setMidDb (12.0f);
    eq.reset();

    const float amplitude = 0.2f;
    const double inputRms = amplitude / std::sqrt (2.0);

    const double midRms = steadyStateRms (eq, 750.0, amplitude, kSettleSamples, kMeasureSamples);
    const double midBoostDb = gainToDb (midRms / inputRms);

    // Right at the peak centre frequency, expect close to the full +12dB.
    CHECK (midBoostDb == Approx (12.0).margin (1.0));

    eq.reset();
    const double lowRms = steadyStateRms (eq, 80.0, amplitude, kSettleSamples, kMeasureSamples);
    const double lowBoostDb = gainToDb (lowRms / inputRms);

    CHECK (lowBoostDb == Approx (0.0).margin (1.5));
}

TEST_CASE ("sg::PostEq +12dB treble/presence shelves boost a 10kHz sine and leave 200Hz nearly unchanged", "[PostEq]")
{
    sg::PostEq eq;
    eq.prepare (kSampleRate, kMaxBlockSize, 1);
    eq.setTrebleDb (12.0f);
    eq.setPresenceDb (12.0f);
    eq.reset();

    const float amplitude = 0.2f;
    const double inputRms = amplitude / std::sqrt (2.0);

    // 10kHz is above both the ~3kHz and ~6.5kHz shelf corners, so both
    // shelves are fully engaged: expect close to the combined +24dB.
    const double highRms = steadyStateRms (eq, 10000.0, amplitude, kSettleSamples, kMeasureSamples);
    const double highBoostDb = gainToDb (highRms / inputRms);
    CHECK (highBoostDb > 18.0);
    CHECK (highBoostDb < 24.5);

    eq.reset();
    const double lowRms = steadyStateRms (eq, 200.0, amplitude, kSettleSamples, kMeasureSamples);
    const double lowBoostDb = gainToDb (lowRms / inputRms);
    CHECK (lowBoostDb == Approx (0.0).margin (1.5));
}

TEST_CASE ("sg::PostEq with all bands at 0dB is approximately a passthrough", "[PostEq]")
{
    sg::PostEq eq;
    eq.prepare (kSampleRate, kMaxBlockSize, 1);
    eq.setBassDb (0.0f);
    eq.setMidDb (0.0f);
    eq.setTrebleDb (0.0f);
    eq.setPresenceDb (0.0f);
    eq.reset();

    const float amplitude = 0.25f;
    const double inputRms = amplitude / std::sqrt (2.0);

    for (double freq : { 60.0, 440.0, 3000.0, 8000.0 })
    {
        eq.reset();
        const double rms = steadyStateRms (eq, freq, amplitude, kSettleSamples, kMeasureSamples);
        const double boostDb = gainToDb (rms / inputRms);
        CHECK (boostDb == Approx (0.0).margin (0.2));
    }

    CHECK (eq.getBassDb() == Approx (0.0f));
    CHECK (eq.getMidDb() == Approx (0.0f));
    CHECK (eq.getTrebleDb() == Approx (0.0f));
    CHECK (eq.getPresenceDb() == Approx (0.0f));
}
