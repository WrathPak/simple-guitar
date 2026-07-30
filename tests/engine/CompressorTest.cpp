// Catch2 v3 tests for sg::Compressor (engine/include/sg/Compressor.h).
//
// API contract exercised here (per the M2 slot-architecture brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void setEnabled(bool) noexcept;         // disabled = clean passthrough
//   void setAmount/setAttack/setRelease/setLevel(float) noexcept; // normalized 0..1
//
// setAmount() moves threshold (-6..-40dB) and ratio (2..8) together; the
// headline behaviour under test is that a heavily compressed ("squeeze")
// setting measurably narrows the dynamic range between a quiet and a loud
// segment, compared to how different those two segments are at the input.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <sg/Compressor.h>

#include <cmath>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    std::vector<float> runMono (sg::Compressor& fx, const std::vector<float>& input)
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

    std::vector<float> sineSegment (float amplitude, double freqHz, size_t length, double sampleRate)
    {
        std::vector<float> v (length);
        for (size_t i = 0; i < length; ++i)
            v[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freqHz * (double) i / sampleRate);
        return v;
    }

    double rmsOf (const std::vector<float>& v, size_t start, size_t length)
    {
        double sumSquares = 0.0;
        for (size_t i = start; i < start + length; ++i)
            sumSquares += (double) v[i] * (double) v[i];
        return std::sqrt (sumSquares / (double) length);
    }
}

TEST_CASE ("sg::Compressor disabled is a bit-exact passthrough", "[Compressor]")
{
    sg::Compressor fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setAmount (1.0f);
    fx.setAttack (0.0f);
    fx.setRelease (0.0f);
    fx.setLevel (1.0f); // even with makeup gain cranked, disabled must still be bit-exact
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

TEST_CASE ("sg::Compressor enabled is finite and deterministic", "[Compressor]")
{
    auto run = [] () {
        sg::Compressor fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setAmount (0.7f);
        fx.setAttack (0.3f);
        fx.setRelease (0.4f);
        fx.setLevel (0.5f);
        fx.reset();

        return runMono (fx, sineSegment (0.6f, 220.0, 8000, kSampleRate));
    };

    const auto a = run();
    const auto b = run();

    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (std::isfinite (a[i]));
        CHECK (a[i] == b[i]);
    }
}

TEST_CASE ("sg::Compressor reduces dynamic range measurably between a quiet and a loud segment", "[Compressor]")
{
    sg::Compressor fx;
    fx.prepare (kSampleRate, kMaxBlockSize, 1);
    fx.setAmount (1.0f);  // heaviest squeeze: threshold -40dB, ratio 8:1
    fx.setAttack (0.0f);  // fastest attack/release so each segment's envelope settles well within it
    fx.setRelease (0.0f);
    fx.setLevel (0.5f);   // unity makeup gain (0.5 -> 0dB, the midpoint of the -12..+12 range)
    fx.reset();

    constexpr double freqHz = 300.0;
    constexpr size_t segmentLength = (size_t) (kSampleRate * 0.3); // 300ms -- plenty for the envelope to settle
    constexpr float quietAmplitude = 0.02f; // well below even the lightest threshold
    constexpr float loudAmplitude = 0.9f;   // well above every threshold

    const auto quietIn = sineSegment (quietAmplitude, freqHz, segmentLength, kSampleRate);
    const auto loudIn = sineSegment (loudAmplitude, freqHz, segmentLength, kSampleRate);

    const auto quietOut = runMono (fx, quietIn);
    const auto loudOut = runMono (fx, loudIn); // continues the same compressor instance/envelope

    // Measure over the second half of each segment only, so the envelope has
    // fully settled and isn't still reacting to the segment boundary.
    const size_t half = segmentLength / 2;
    const double quietRmsIn = rmsOf (quietIn, half, segmentLength - half);
    const double quietRmsOut = rmsOf (quietOut, half, segmentLength - half);
    const double loudRmsIn = rmsOf (loudIn, half, segmentLength - half);
    const double loudRmsOut = rmsOf (loudOut, half, segmentLength - half);

    const double inputRatioDb = 20.0 * std::log10 (loudRmsIn / quietRmsIn);
    const double outputRatioDb = 20.0 * std::log10 (loudRmsOut / quietRmsOut);

    REQUIRE (inputRatioDb > 20.0); // sanity: the two test amplitudes really are very different at the input

    // The compressor must measurably narrow that gap -- generous margin
    // (well short of perfect 8:1 tracking, which the envelope follower's
    // dynamics wouldn't hit exactly anyway) so this isn't a brittle,
    // over-precise assertion about the exact compression curve.
    CHECK (outputRatioDb < inputRatioDb * 0.6);
}

TEST_CASE ("sg::Compressor level control raises output level", "[Compressor]")
{
    auto runAtLevel = [] (float level01) {
        sg::Compressor fx;
        fx.prepare (kSampleRate, kMaxBlockSize, 1);
        fx.setAmount (0.5f);
        fx.setAttack (0.5f);
        fx.setRelease (0.5f);
        fx.setLevel (level01);
        fx.reset();

        const auto input = sineSegment (0.3f, 250.0, (size_t) (kSampleRate * 0.2), kSampleRate);
        const auto output = runMono (fx, input);
        return rmsOf (output, output.size() / 2, output.size() / 2);
    };

    const double quietLevelRms = runAtLevel (0.1f);
    const double loudLevelRms = runAtLevel (0.9f);

    CHECK (loudLevelRms > quietLevelRms * 2.0);
}
