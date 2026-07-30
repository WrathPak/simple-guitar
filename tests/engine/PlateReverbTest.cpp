// Catch2 v3 tests for sg::PlateReverb (engine/include/sg/PlateReverb.h).
//
// API contract exercised here (per the M2 pedals brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;        // disabled = clean passthrough
//   void setDecay/setTone/setMix(float) noexcept;  // normalized 0..1
//   Mix = 0 must be bit-exact dry.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/PlateReverb.h>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    std::vector<float> runMono (sg::PlateReverb& fx, const std::vector<float>& input)
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

    double windowRms (const std::vector<float>& data, size_t startSample, size_t length)
    {
        double sumSquares = 0.0;
        size_t count = 0;
        for (size_t i = startSample; i < std::min (data.size(), startSample + length); ++i)
        {
            sumSquares += (double) data[i] * (double) data[i];
            ++count;
        }
        return count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;
    }
}

TEST_CASE ("sg::PlateReverb disabled is a bit-exact passthrough", "[PlateReverb]")
{
    sg::PlateReverb fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setDecay (0.8f);
    fx.setTone (0.8f);
    fx.setMix (0.8f);
    fx.setEnabled (false);
    fx.reset();

    juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample (0, i, (float) std::sin (0.25 * i) * 0.6f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    fx.process (buffer);

    for (int i = 0; i < kMaxBlockSize; ++i)
        CHECK (buffer.getSample (0, i) == reference.getSample (0, i));

    CHECK (fx.isEnabled() == false);
}

TEST_CASE ("sg::PlateReverb mix = 0 is bit-exact dry even with dramatic decay/tone settings", "[PlateReverb]")
{
    sg::PlateReverb fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setDecay (1.0f);
    fx.setTone (1.0f);
    fx.setMix (0.0f);
    fx.reset();

    juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
    for (int i = 0; i < kMaxBlockSize; ++i)
        buffer.setSample (0, i, (float) std::sin (0.15 * i) * 0.5f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    // Run several blocks (not just one) to prove mix=0 stays bit-exact
    // dry over time, not just on the very first block.
    for (int block = 0; block < 8; ++block)
    {
        reference.makeCopyOf (buffer);
        fx.process (buffer);

        for (int i = 0; i < kMaxBlockSize; ++i)
            CHECK (buffer.getSample (0, i) == reference.getSample (0, i));
    }
}

TEST_CASE ("sg::PlateReverb enabled with neutral-ish settings is finite, non-silent and deterministic", "[PlateReverb]")
{
    auto run = [] () {
        sg::PlateReverb fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setDecay (0.5f);
        fx.setTone (0.5f);
        fx.setMix (0.4f);
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

TEST_CASE ("sg::PlateReverb impulse tail RMS decays over time", "[PlateReverb]")
{
    sg::PlateReverb fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setDecay (0.6f);
    fx.setTone (0.5f);
    fx.setMix (1.0f); // fully wet -- isolate the reverb tail
    fx.reset();

    const auto output = runMono (fx, impulse (96000)); // 2s @ 48kHz

    const size_t windowLen = 4800; // 100ms
    const double earlyRms = windowRms (output, windowLen, windowLen);     // 100ms..200ms
    const double lateRms = windowRms (output, 76800, windowLen);          // 1.6s..1.7s

    CHECK (earlyRms > 0.0);
    CHECK (lateRms < earlyRms); // the tail has audibly decayed by 1.6s in
}

TEST_CASE ("sg::PlateReverb longer decay setting yields a measurably longer tail", "[PlateReverb]")
{
    auto tailRmsAt = [] (float decay01, size_t startSample, size_t length) {
        sg::PlateReverb fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setDecay (decay01);
        fx.setTone (0.5f);
        fx.setMix (1.0f);
        fx.reset();

        const auto output = runMono (fx, impulse (startSample + length + 4800));
        return windowRms (output, startSample, length);
    };

    const size_t windowLen = 4800;      // 100ms
    const size_t lateStart = 62400;     // 1.3s in -- long past a short room's decay

    const double shortDecayTail = tailRmsAt (0.0f, lateStart, windowLen);
    const double longDecayTail = tailRmsAt (1.0f, lateStart, windowLen);

    CHECK (longDecayTail > shortDecayTail * 3.0); // clearly longer-lived tail
}
