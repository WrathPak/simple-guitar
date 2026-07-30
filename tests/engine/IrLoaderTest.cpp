// Catch2 v3 tests for sg::IrLoader (engine/include/sg/IrLoader.h).
//
// API contract exercised here (per the M1 engine-blocks brief):
//   void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
//   void process(juce::AudioBuffer<float>&) noexcept;
//   void reset() noexcept;
//   void requestLoad(const juce::String& wavPath,
//                     std::function<void(bool ok, juce::String errorOrName)> onDone);  // message thread
//   bool isLoaded() const noexcept;
//   juce::String getIrName() const noexcept;
//   void setEnabled(bool) noexcept;
//   void setLowCutHz(float) noexcept;    // 20..400
//   void setHighCutHz(float) noexcept;   // 2000..20000
//
// These tests generate small WAV impulse responses on disk (via
// juce::WavAudioFormat) rather than depending on any checked-in fixture.
//
// Note on requestLoad() timing: juce::dsp::Convolution loads impulse
// responses on its own background thread (a real std::thread polling on a
// ~10ms cadence) and then crossfades the new engine in. requestLoad()'s
// onDone callback fires as soon as the request has been *handed off*, not
// once JUCE's background thread has actually finished swapping the engine
// in. A test that calls requestLoad() and immediately starts crunching
// through samples (with no real wall-clock delay between them, unlike a
// live audio callback) can measure a mix of the old/default engine and the
// new one. waitForBackgroundIrLoad() below gives that background thread a
// generous real-time window to finish before we measure anything.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <sg/IrLoader.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kMaxBlockSize = 128;

    // See the file-level comment above: gives Convolution's background IR
    // loading thread time to finish and crossfade in before a test measures
    // anything. Generous on purpose -- this is offline test code, not
    // audio-thread code, so a real sleep is fine here.
    void waitForBackgroundIrLoad()
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }

    // juce::dsp::Convolution only actually installs a newly-loaded engine
    // and runs its (internal, fixed ~50ms) old/new crossfade *while
    // process() is being called* -- wall-clock time alone (see
    // waitForBackgroundIrLoad() above) is necessary but not sufficient. A
    // test that loads an IR and then immediately measures a handful of
    // samples can end up mostly hearing the previous (default, unity-gain)
    // engine rather than the one it just requested. Feed a generous run of
    // silence through process() first so the swap is guaranteed to have
    // completed before anything gets measured.
    void warmUpEngineSwap (sg::IrLoader& loader, int numSamples = 12000)
    {
        juce::AudioBuffer<float> buffer (1, kMaxBlockSize);
        int remaining = numSamples;

        while (remaining > 0)
        {
            const int thisBlock = std::min (remaining, kMaxBlockSize);
            buffer.setSize (1, thisBlock, false, false, true);
            buffer.clear();
            loader.process (buffer);
            remaining -= thisBlock;
        }
    }

    juce::File tempDirForTests()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sg_ir_loader_tests");
        dir.createDirectory();
        return dir;
    }

    // Writes a mono 32-bit float WAV file containing `samples`, returning the file.
    juce::File writeMonoWav (const juce::String& fileName, const std::vector<float>& samples)
    {
        auto file = tempDirForTests().getChildFile (fileName);
        file.deleteFile();

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        REQUIRE (stream != nullptr);

        const auto options = juce::AudioFormatWriterOptions {}
                                  .withSampleRate (kSampleRate)
                                  .withNumChannels (1)
                                  .withBitsPerSample (32)
                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (stream, options));
        REQUIRE (writer != nullptr);

        juce::AudioBuffer<float> buffer (1, (int) samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            buffer.setSample (0, (int) i, samples[i]);

        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, (int) samples.size()));
        writer.reset(); // flush + close before Convolution reopens the file

        return file;
    }

    double steadyStateRms (sg::IrLoader& loader, double frequencyHz, float amplitude,
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

                loader.process (buffer);

                if (sumSquaresOut != nullptr)
                    for (int i = 0; i < thisBlock; ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        *sumSquaresOut += (double) s * (double) s;
                    }

                remaining -= thisBlock;
            }
        };

        feed (settleSamples, nullptr);
        double sumSquares = 0.0;
        feed (measureSamples, &sumSquares);
        return std::sqrt (sumSquares / (double) measureSamples);
    }
}

TEST_CASE ("sg::IrLoader loads a unit-impulse IR and is near passthrough (up to normalisation) for an in-band sine", "[IrLoader]")
{
    const auto file = writeMonoWav ("unit_impulse.wav", { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });

    sg::IrLoader loader;
    loader.prepare (kSampleRate, kMaxBlockSize, 1);

    CHECK (loader.isLoaded() == false);
    CHECK (loader.getIrName().isEmpty());

    bool callbackOk = false;
    juce::String callbackName;
    loader.requestLoad (file.getFullPathName(), [&] (bool ok, const juce::String& nameOrError)
    {
        callbackOk = ok;
        callbackName = nameOrError;
    });

    CHECK (callbackOk == true);
    CHECK (callbackName == "unit_impulse");
    CHECK (loader.isLoaded() == true);
    CHECK (loader.getIrName() == "unit_impulse");

    waitForBackgroundIrLoad();
    warmUpEngineSwap (loader);

    // Push low/high cut to their least-intrusive extremes so a mid-band
    // signal sees an (almost) flat cab response.
    loader.setLowCutHz (sg::IrLoader::minLowCutHz);
    loader.setHighCutHz (sg::IrLoader::maxHighCutHz);
    loader.reset();

    const float amplitude = 0.3f;
    const double inputRms = amplitude / std::sqrt (2.0);
    const double outputRms = steadyStateRms (loader, 1000.0, amplitude, 4000, 4000);

    // juce::dsp::Convolution's Normalise::yes doesn't normalise to unity
    // gain -- it scales the IR so its total energy matches a fixed
    // reference (gain = 0.125 / sqrt(sum of squared IR samples)). Our IR is
    // a single full-scale sample after Trim removes the trailing silence, so
    // sum-of-squares = 1.0 and the expected steady-state gain is exactly
    // 0.125 (confirmed against a bare juce::dsp::Convolution instance).
    constexpr double kExpectedNormalisedGain = 0.125;
    CHECK (outputRms == Approx (inputRms * kExpectedNormalisedGain).margin (inputRms * kExpectedNormalisedGain * 0.2));
}

TEST_CASE ("sg::IrLoader convolves a simple 2-tap echo IR within tolerance", "[IrLoader]")
{
    // A simple one-sample-delay echo IR, [1.0, 0.9], is a two-point comb
    // filter: |H(f)| = |1 + 0.9 * e^-j2*pi*f/fs|. That's at its maximum
    // (1.9x) at DC, falling monotonically to its minimum (0.1x) at Nyquist
    // (fs/2), where the one-sample delay is exactly a half-cycle and the two
    // taps nearly cancel. At fs=48kHz with our low probe at 100Hz (~DC) and
    // high probe at 20kHz (5/6 of the way to the fs/2=24kHz null), the
    // theoretical |H| ratio is ~3.8x (~11.6dB) -- not the full 19x span, but
    // still a large, easily measurable asymmetry.
    //
    // We verify *that* comb shape rather than the IR's raw time-domain
    // samples: IrLoader's own low-cut/high-cut filters are always in the
    // signal path (by contract, there's no way to bypass just those and
    // keep convolution active), and any real 2nd-order filter -- even set
    // to its least intrusive corner -- has a non-negligible few-sample-wide
    // impulse response of its own, which would smear an exact
    // sample-by-sample comparison of adjacent IR taps. The comb's low-vs-high
    // asymmetry survives that smearing comfortably (if anything, our own
    // high-cut filter's roll-off right at 20kHz reinforces it further).
    const auto file = writeMonoWav ("two_tap_echo.wav", { 1.0f, 0.9f });

    sg::IrLoader loader;
    loader.prepare (kSampleRate, kMaxBlockSize, 1);

    bool callbackOk = false;
    loader.requestLoad (file.getFullPathName(), [&] (bool ok, const juce::String&) { callbackOk = ok; });
    REQUIRE (callbackOk);
    REQUIRE (loader.isLoaded());

    waitForBackgroundIrLoad();
    warmUpEngineSwap (loader);

    loader.setLowCutHz (sg::IrLoader::minLowCutHz);
    loader.setHighCutHz (sg::IrLoader::maxHighCutHz);
    loader.reset();

    const float amplitude = 0.2f;
    const double lowRms = steadyStateRms (loader, 100.0, amplitude, 4000, 4000);
    loader.reset();
    const double highRms = steadyStateRms (loader, 20000.0, amplitude, 4000, 4000);

    REQUIRE (lowRms > 0.0);
    REQUIRE (highRms > 0.0);

    const double combDb = 20.0 * std::log10 (lowRms / highRms);
    CHECK (combDb > 6.0); // comfortably under the ~11.6dB theoretical asymmetry
}

TEST_CASE ("sg::IrLoader low-cut attenuates a 30Hz sine when set to 200Hz", "[IrLoader]")
{
    const auto file = writeMonoWav ("unit_impulse_for_lowcut.wav", { 1.0f, 0.0f, 0.0f, 0.0f });

    sg::IrLoader loader;
    loader.prepare (kSampleRate, kMaxBlockSize, 1);

    bool callbackOk = false;
    loader.requestLoad (file.getFullPathName(), [&] (bool ok, const juce::String&) { callbackOk = ok; });
    REQUIRE (callbackOk);

    waitForBackgroundIrLoad();
    warmUpEngineSwap (loader);

    loader.setHighCutHz (sg::IrLoader::maxHighCutHz);

    const float amplitude = 0.3f;
    const double inputRms = amplitude / std::sqrt (2.0);

    // Same normalised-gain reasoning as the passthrough test above: a
    // single full-scale-sample IR settles to a fixed 0.125x gain, regardless
    // of our own low/high cut settings (those are a separate stage in the
    // chain, applied on top of whatever Convolution's engine outputs).
    constexpr double kExpectedNormalisedGain = 0.125;
    const double expectedFlatRms = inputRms * kExpectedNormalisedGain;

    // Baseline: low cut at its minimum should barely touch a 30Hz tone.
    loader.setLowCutHz (sg::IrLoader::minLowCutHz);
    loader.reset();
    const double baselineRms = steadyStateRms (loader, 30.0, amplitude, 32000, 16000);
    CHECK (baselineRms == Approx (expectedFlatRms).margin (expectedFlatRms * 0.3));

    // With the low cut raised well above the signal, expect heavy attenuation
    // relative to that same flat/passthrough reference level.
    loader.setLowCutHz (200.0f);
    loader.reset();
    const double attenuatedRms = steadyStateRms (loader, 30.0, amplitude, 32000, 16000);

    const double attenuationDb = 20.0 * std::log10 (std::max (attenuatedRms, 1.0e-12) / expectedFlatRms);
    CHECK (attenuationDb < -15.0);
}
