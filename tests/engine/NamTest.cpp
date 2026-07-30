// Catch2 v3 tests for sg::NamProcessor (engine/include/sg/NamProcessor.h).
//
// Fixture: these tests load NeuralAmpModelerCore's own bundled example model,
// example_models/lstm.nam (a tiny single-layer LSTM, ~2KB, expected sample
// rate 48000Hz, metadata.name == "Test LSTM", carries loudness metadata),
// via the SG_NAM_TEST_MODEL_PATH path defined by the CMake build (see
// tests/CMakeLists.txt). No hand-authored .nam JSON is needed since this
// fixture already exists in the fetched dependency source tree.
//
// requestLoad()'s completion callback is delivered asynchronously on the
// JUCE message thread (see NamProcessor.h), so these tests need to pump the
// message loop to observe it. That requires JUCE_MODAL_LOOPS_PERMITTED=1 to
// be defined wherever juce_events is compiled (see engine/CMakeLists.txt).
//
// API contract exercised here:
//   sg::NamProcessor
//     void prepare(double sampleRate, int maxBlockSize) noexcept;
//     void process(juce::AudioBuffer<float>& buffer) noexcept;   // stereo in/out, mono-summed internally
//     void reset() noexcept;
//     void requestLoad(const juce::String& namFilePath,
//                       std::function<void(bool ok, juce::String errorOrName)> onDone);
//     bool isLoaded() const noexcept;
//     juce::String getModelName() const noexcept;
//     void setNormalize(bool) noexcept;
//     double getModelSampleRate() const noexcept;

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <sg/NamProcessor.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>

#if !defined (SG_NAM_TEST_MODEL_PATH)
  #error "SG_NAM_TEST_MODEL_PATH must be defined by the build to a .nam fixture path " \
         "(NeuralAmpModelerCore's example_models/lstm.nam) -- see tests/CMakeLists.txt."
#endif

using Catch::Approx;

namespace
{
    const juce::String kTestModelPath = SG_NAM_TEST_MODEL_PATH;

    // juce::MessageManager binds "the message thread" identity to whichever
    // thread constructs its singleton first (see its ctor: messageThreadId
    // (Thread::getCurrentThreadId())). Left to chance, that race could be won
    // by a NamProcessor background loader thread instead of this test
    // binary's real main thread (loader threads also touch the singleton, via
    // MessageManager::callAsync's own internal getInstance() call) -- and if
    // it is, every requestLoad() callback is posted to a thread that's about
    // to exit and never pumps, so it's lost forever and pumpUntil() below
    // times out no matter how fast or slow the load actually is.
    //
    // Claiming the identity has to happen from inside main(), not from a
    // namespace-scope static object's constructor: those run during CRT
    // static initialization, before main() starts, which is too early for
    // JUCE's Win32-backed MessageManager to safely stand up (this crashed
    // with an access violation during Catch2's own test-discovery run when
    // tried that way). A Catch2 event listener's testRunStarting() fires
    // once, from inside Catch2's Session::run() -- i.e. properly inside
    // main() -- and always before any TEST_CASE (and therefore before any
    // requestLoad()) executes, which is exactly what's needed here and is
    // what a real JUCE app's own startup sequence would already have done
    // long before any plugin/engine code could run.
    struct MessageThreadClaimListener : Catch::EventListenerBase
    {
        using EventListenerBase::EventListenerBase;

        void testRunStarting (Catch::TestRunInfo const&) override
        {
            juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        }
    };

    // Pumps the JUCE message loop (in short slices) until `predicate` is
    // true, or throws after `timeoutMs`. Requires JUCE_MODAL_LOOPS_PERMITTED.
    void pumpUntil (const std::function<bool()>& predicate, int timeoutMs = 5000)
    {
        auto* mm = juce::MessageManager::getInstance();
        const auto start = std::chrono::steady_clock::now();

        while (! predicate())
        {
            mm->runDispatchLoopUntil (5);

            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now() - start).count();
            if (elapsedMs > timeoutMs)
                throw std::runtime_error ("Timed out waiting for an async NamProcessor callback");
        }
    }

    // Issues requestLoad() and blocks (via pumpUntil) until it completes.
    // Throws if the load reports failure.
    juce::String loadAndWait (sg::NamProcessor& nam, const juce::String& path)
    {
        bool done = false;
        bool ok = false;
        juce::String message;

        nam.requestLoad (path, [&] (bool success, juce::String result)
        {
            ok = success;
            message = result;
            done = true;
        });

        pumpUntil ([&] { return done; });

        if (! ok)
            throw std::runtime_error (("requestLoad failed: " + message).toStdString());

        return message;
    }

    float sineSample (double freqHz, double sampleRate, int64_t sampleIndex)
    {
        return (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz
                                  * (double) sampleIndex / sampleRate);
    }

    void fillSineStereo (juce::AudioBuffer<float>& buffer, double freqHz, double sampleRate, int64_t startSample)
    {
        const int numSamples = buffer.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = sineSample (freqHz, sampleRate, startSample + i);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, s);
        }
    }
}

CATCH_REGISTER_LISTENER (MessageThreadClaimListener)

//==============================================================================
TEST_CASE ("sg::NamProcessor requestLoad succeeds and reports the model's name", "[NamProcessor]")
{
    sg::NamProcessor nam;
    nam.prepare (48000.0, 64);

    CHECK_FALSE (nam.isLoaded());
    CHECK (nam.getModelName().isEmpty());
    CHECK (nam.getModelSampleRate() == 0.0);

    const juce::String reportedName = loadAndWait (nam, kTestModelPath);

    CHECK (reportedName == "Test LSTM"); // from lstm.nam's metadata.name
    CHECK (nam.isLoaded());
    CHECK (nam.getModelName() == "Test LSTM");
    CHECK (nam.getModelSampleRate() == Approx (48000.0));
}

TEST_CASE ("sg::NamProcessor requestLoad reports failure for a nonexistent file", "[NamProcessor]")
{
    sg::NamProcessor nam;
    nam.prepare (48000.0, 64);

    bool done = false;
    bool ok = true;
    juce::String message;

    nam.requestLoad ("C:/definitely/not/a/real/path/nope.nam", [&] (bool success, juce::String result)
    {
        ok = success;
        message = result;
        done = true;
    });

    pumpUntil ([&] { return done; });

    CHECK_FALSE (ok);
    CHECK (message.isNotEmpty());
    CHECK_FALSE (nam.isLoaded());
}

//==============================================================================
TEST_CASE ("sg::NamProcessor produces finite, non-silent, deterministic output for a 1kHz sine", "[NamProcessor]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr int numBlocks = 100;

    auto runOnce = [&]
    {
        sg::NamProcessor nam;
        nam.prepare (sampleRate, blockSize);
        loadAndWait (nam, kTestModelPath);
        REQUIRE (nam.isLoaded());

        std::vector<float> out;
        out.reserve ((size_t) (blockSize * numBlocks));

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            fillSineStereo (buffer, 1000.0, sampleRate, (int64_t) b * blockSize);
            nam.process (buffer);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
        return out;
    };

    const auto run1 = runOnce();
    const auto run2 = runOnce();

    REQUIRE (run1.size() == run2.size());

    bool anyNonSilent = false;
    for (size_t i = 0; i < run1.size(); ++i)
    {
        REQUIRE (std::isfinite (run1[i]));
        if (std::abs (run1[i]) > 1.0e-6f)
            anyNonSilent = true;

        // Two independently-loaded instances processing identical input must
        // produce bit-identical output: no RNG, no threading nondeterminism
        // in the DSP forward pass.
        CHECK (run1[i] == run2[i]);
    }

    CHECK (anyNonSilent);
}

//==============================================================================
TEST_CASE ("sg::NamProcessor passes audio through unchanged when nothing is loaded", "[NamProcessor]")
{
    sg::NamProcessor nam;
    nam.prepare (48000.0, 64);
    // Deliberately never call requestLoad().

    constexpr int blockSize = 64;
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        buffer.setSample (0, i, 0.5f);
        buffer.setSample (1, i, -0.25f);
    }

    const juce::AudioBuffer<float> reference (buffer);

    nam.process (buffer);

    for (int i = 0; i < blockSize; ++i)
    {
        CHECK (buffer.getSample (0, i) == Approx (reference.getSample (0, i)));
        CHECK (buffer.getSample (1, i) == Approx (reference.getSample (1, i)));
    }

    CHECK_FALSE (nam.isLoaded());
}

//==============================================================================
TEST_CASE ("sg::NamProcessor requestLoad during continuous processing has no discontinuity", "[NamProcessor]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;

    sg::NamProcessor nam;
    nam.prepare (sampleRate, blockSize);
    loadAndWait (nam, kTestModelPath);
    REQUIRE (nam.isLoaded());

    std::vector<float> captured;
    captured.reserve (200000);
    std::atomic<bool> keepGoing { true };
    std::atomic<int64_t> samplesProcessed { 0 };

    // Simulates the audio thread: continuously processes blocks of a 1kHz
    // sine while, concurrently, a second requestLoad() (on this same test
    // thread, below) swaps the model out from under it. Paced to real block
    // time (not run flat-out) so this test has bounded, predictable wall
    // time and memory regardless of how fast this particular model runs.
    const auto blockPeriod = std::chrono::duration<double> ((double) blockSize / sampleRate);

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        int64_t pos = 0;
        while (keepGoing.load (std::memory_order_relaxed))
        {
            fillSineStereo (buffer, 1000.0, sampleRate, pos);
            nam.process (buffer);
            for (int i = 0; i < blockSize; ++i)
                captured.push_back (buffer.getSample (0, i));
            pos += blockSize;
            std::this_thread::sleep_for (blockPeriod);
            samplesProcessed.store (pos, std::memory_order_relaxed);
        }
    });

    // Let a clean, steady-state run build up before triggering the swap.
    while (samplesProcessed.load (std::memory_order_relaxed) < blockSize * 40)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    // Read via the atomic counter, not captured.size() -- the audio thread is
    // still concurrently push_back()-ing into `captured` at this point, and
    // reading its size from another thread while that's happening would be a
    // data race. Every index below this count is already fully written by
    // now (the thread stores samplesProcessed strictly after appending), so
    // it's safe to use once we index into `captured` after the join() below.
    const size_t baselineSampleCount = (size_t) samplesProcessed.load (std::memory_order_relaxed);

    bool secondLoadDone = false;
    nam.requestLoad (kTestModelPath, [&] (bool ok, juce::String)
    {
        secondLoadDone = ok;
    });

    pumpUntil ([&] { return secondLoadDone; });

    // Let the audio thread run a bit further so the crossfade fully completes
    // and gets captured.
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    keepGoing.store (false, std::memory_order_relaxed);
    audioThread.join();

    REQUIRE (captured.size() > (size_t) (blockSize * 60));

    for (float v : captured)
        REQUIRE (std::isfinite (v));

    auto maxDeltaInRange = [&] (size_t begin, size_t end)
    {
        float m = 0.0f;
        for (size_t i = begin + 1; i < end; ++i)
            m = std::max (m, std::abs (captured[i] - captured[i - 1]));
        return m;
    };

    // Baseline: normal sample-to-sample delta well before the swap, i.e. what
    // this signal/model naturally looks like with no crossfade in progress.
    const size_t baselineEnd = std::min (baselineSampleCount, captured.size());
    const float baselineDelta = maxDeltaInRange (0, baselineEnd);

    // The swap/crossfade region should not introduce a delta wildly larger
    // than the signal's own natural baseline -- that would indicate a click.
    // A real click is a near-instantaneous jump (many multiples of the
    // signal's own step size); a smooth crossfade stays close to it.
    const float overallDelta = maxDeltaInRange (0, captured.size());

    CHECK (overallDelta < (baselineDelta * 6.0f) + 1.0e-4f);
}

//==============================================================================
TEST_CASE ("sg::NamProcessor resamples a 48kHz model when the session runs at 44.1kHz", "[NamProcessor]")
{
    constexpr double sessionRate = 44100.0;
    constexpr int blockSize = 64;
    constexpr int numBlocks = 200;

    sg::NamProcessor nam;
    nam.prepare (sessionRate, blockSize);
    loadAndWait (nam, kTestModelPath);

    REQUIRE (nam.isLoaded());
    CHECK (nam.getModelSampleRate() == Approx (48000.0)); // resampler must be engaged: model rate != session rate

    std::vector<float> out;
    out.reserve ((size_t) (blockSize * numBlocks));

    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int b = 0; b < numBlocks; ++b)
    {
        fillSineStereo (buffer, 1000.0, sessionRate, (int64_t) b * blockSize);
        nam.process (buffer);
        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    bool anyNonSilent = false;
    float maxAbs = 0.0f;
    for (float v : out)
    {
        REQUIRE (std::isfinite (v));
        maxAbs = std::max (maxAbs, std::abs (v));
        if (std::abs (v) > 1.0e-6f)
            anyNonSilent = true;
    }

    CHECK (anyNonSilent);
    CHECK (maxAbs < 100.0f); // generous sanity bound: no runaway/garbage from the resampler
}

//==============================================================================
TEST_CASE ("sg::NamProcessor throughput: 10s of audio at 48k/64 processes well under 10s wall clock", "[NamProcessor]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr int totalSamples = (int) sampleRate * 10;
    constexpr int numBlocks = totalSamples / blockSize;

    sg::NamProcessor nam;
    nam.prepare (sampleRate, blockSize);
    loadAndWait (nam, kTestModelPath);
    REQUIRE (nam.isLoaded());

    juce::AudioBuffer<float> buffer (2, blockSize);

    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < numBlocks; ++b)
    {
        fillSineStereo (buffer, 1000.0, sampleRate, (int64_t) b * blockSize);
        nam.process (buffer);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double seconds = std::chrono::duration<double> (elapsed).count();

    INFO ("Processed " << numBlocks << " blocks (" << (numBlocks * blockSize) << " samples, "
                        << (numBlocks * blockSize / sampleRate) << "s of audio) in " << seconds << "s wall clock");
    CHECK (seconds < 10.0);
}
