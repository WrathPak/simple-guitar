// Validates every bundled factory .nam capture (content/models/*.nam --
// see content/ATTRIBUTION.md) through the real loader, sg::NamProcessor::requestLoad,
// the same class NamProcessor.h/NamTest.cpp exercise for hand-authored/fixture
// models. This is a content-verification test, not a NamProcessor API test
// (see tests/engine/NamTest.cpp for that) -- it exists so a bad or
// incompatible download (wrong architecture, truncated file, wrong sample
// rate) fails the build instead of silently shipping.
//
// SG_CONTENT_MODELS_DIR is defined by tests/CMakeLists.txt to the absolute
// path of content/models/ -- this test lists whatever *.nam files are
// actually there at build time rather than hardcoding the current bundled
// set, so it stays correct as the factory content bundle changes.
//
// Reuses the same message-thread-claiming/pumpUntil scaffolding as
// NamTest.cpp (duplicated here rather than shared, since NamTest.cpp keeps
// its helpers in an anonymous namespace private to that translation unit --
// see PresetStore.cpp's own comment on the identical tradeoff versus
// RigLibrary.cpp).

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <sg/NamProcessor.h>

#include <chrono>
#include <functional>
#include <stdexcept>
#include <vector>

#if !defined (SG_CONTENT_MODELS_DIR)
  #error "SG_CONTENT_MODELS_DIR must be defined by the build to content/models/'s absolute path " \
         "-- see tests/CMakeLists.txt."
#endif

namespace
{
    struct BundledNamMessageThreadClaimListener : Catch::EventListenerBase
    {
        using EventListenerBase::EventListenerBase;

        void testRunStarting (Catch::TestRunInfo const&) override
        {
            // Idempotent: NamTest.cpp's own listener may have already claimed
            // this for the same test run when both translation units link
            // into the same sg_tests binary -- setCurrentThreadAsMessageThread()
            // is safe to call more than once from the same (real) thread.
            juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        }
    };

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

    std::vector<juce::File> listBundledNamFiles()
    {
        const juce::File modelsDir (SG_CONTENT_MODELS_DIR);
        std::vector<juce::File> files;

        for (const auto& item : juce::RangedDirectoryIterator (modelsDir, false, "*.nam", juce::File::findFiles))
            files.push_back (item.getFile());

        return files;
    }
}

CATCH_REGISTER_LISTENER (BundledNamMessageThreadClaimListener)

TEST_CASE ("every bundled factory .nam capture loads via sg::NamProcessor::requestLoad", "[content][NamProcessor]")
{
    const auto files = listBundledNamFiles();

    // If this fires, content/models/ is empty or SG_CONTENT_MODELS_DIR points
    // at the wrong place -- not a loader failure, but just as important to
    // catch (an empty bundle would otherwise pass silently).
    REQUIRE (! files.empty());

    for (const auto& file : files)
    {
        INFO ("model: " << file.getFileName());

        sg::NamProcessor nam;
        nam.prepare (48000.0, 64);

        bool done = false;
        bool ok = false;
        juce::String message;

        nam.requestLoad (file.getFullPathName(), [&] (bool success, juce::String result)
        {
            ok = success;
            message = result;
            done = true;
        });

        pumpUntil ([&] { return done; });

        INFO ("result: " << message);
        CHECK (ok);
        CHECK (nam.isLoaded());
        CHECK (nam.getModelSampleRate() > 0.0);

        // A quick, cheap sanity pass: run a little silence through the freshly
        // loaded model and make sure it doesn't produce garbage. Not a tone
        // check (nobody is listening -- see the packaging brief), just a
        // finite-output guard.
        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        nam.process (buffer);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                CHECK (std::isfinite (buffer.getSample (ch, i)));
    }
}
